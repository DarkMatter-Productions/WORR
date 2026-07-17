/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "vk_postprocess.h"

#include "vk_bloom_spv.h"
#include "vk_crt_spv.h"
#include "vk_dof_spv.h"
#include "vk_debug.h"
#include "vk_entity.h"
#include "vk_postprocess_spv.h"
#include "vk_ui.h"
#include "vk_world.h"

#include "renderer/view_setup.h"

#include <math.h>
#include <string.h>

typedef struct {
    float time;
    float waterwarp;
    float color_enabled;
    float brightness;
    float contrast;
    float saturation;
    float output_size[2];
    float tint[4];
    float split_params[4];
    float split_shadow[4];
    float split_highlight[4];
    float lut_params[4];
    float bloom_final[4];
} vk_postprocess_push_t;

typedef struct {
    float output_size[4];
    float params[4];
    float aux[4];
} vk_postprocess_bloom_push_t;

typedef struct {
    float params[4];
    float params2[4];
    float texel[4];
} vk_postprocess_crt_push_t;

typedef struct {
    float params[4];
    float projection[4];
    float rect[4];
} vk_postprocess_dof_push_t;

_Static_assert(sizeof(vk_postprocess_push_t) == 128,
               "final post-process push constants must match vk_postprocess.frag");
_Static_assert(sizeof(vk_postprocess_bloom_push_t) == 48,
               "bloom push constants must match vk_bloom.frag");
_Static_assert(sizeof(vk_postprocess_crt_push_t) == 48,
               "CRT push constants must match vk_crt.frag");
_Static_assert(sizeof(vk_postprocess_dof_push_t) == 48,
               "DOF push constants must match vk_dof.frag");

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    // The sampling view includes the bloom mip chain. The render view is
    // deliberately level zero only, because all fullscreen passes target
    // the base bloom resolution.
    VkImageView view;
    VkImageView render_view;
    VkFramebuffer framebuffer;
    uint32_t mip_levels;
    bool initialized;
    bool mip_chain_initialized;
} vk_postprocess_bloom_image_t;

typedef struct {
    vk_postprocess_bloom_image_t bloom_ping;
    vk_postprocess_bloom_image_t bloom_pong;
    vk_postprocess_bloom_image_t dof_ping;
    vk_postprocess_bloom_image_t dof_pong;
    vk_postprocess_bloom_image_t dof_scene;
    vk_postprocess_bloom_image_t auto_exposure[2];
    VkDescriptorSet final_scene_descriptor_set;
    VkDescriptorSet bloom_scene_descriptor_set;
    VkDescriptorSet bloom_ping_descriptor_set;
    VkDescriptorSet bloom_pong_descriptor_set;
    VkDescriptorSet dof_scene_descriptor_set;
    VkDescriptorSet dof_ping_descriptor_set;
    VkDescriptorSet dof_pong_descriptor_set;
    VkDescriptorSet dof_composite_descriptor_set;
    VkDescriptorSet auto_exposure_descriptor_set;
    bool descriptor_lut_active;
    bool descriptor_bloom_active;
    bool descriptor_show_bloom_active;
    bool descriptor_bloom_emission_active;
    bool descriptor_dof_active;
    bool descriptor_hdr_auto_active;
    uint32_t descriptor_generation;
    VkBuffer blur_kernel_buffer;
    VkDeviceMemory blur_kernel_memory;
    void *blur_kernel_mapped;
    VkDescriptorSet blur_kernel_descriptor_set;
    // The final shader statically declares the exposure sampler even while
    // automatic exposure is disabled. Keep a separate set for the direct
    // float-scene presentation path so that inactive binding still names the
    // scene image actually transitioned to shader-read layout.
    VkDescriptorSet direct_scene_kernel_descriptor_set;
    VkDescriptorSet auto_exposure_kernel_descriptor_set;
    float blur_kernel_sigma;
    bool blur_kernel_initialized;
    uint32_t auto_exposure_index;
    bool auto_exposure_valid;
} vk_postprocess_frame_resources_t;

enum {
    VK_BLOOM_MODE_COPY,
    VK_BLOOM_MODE_PREFILTER,
    VK_BLOOM_MODE_BLUR_X,
    VK_BLOOM_MODE_BLUR_Y,
    // sigma is clamped to 25, which gives a radius of 50. Pairing adjacent
    // samples leaves at most 51 filtered samples, including an odd endpoint.
    VK_POSTPROCESS_BLUR_MAX_PAIRS = 51,
    VK_POSTPROCESS_BLOOM_MAX_LEVELS = 6,
};

typedef struct {
    // std140 vec4 array: x is the bilinear sample offset and y is the
    // unnormalised paired Gaussian weight. z/w remain padding.
    float offset_weight[VK_POSTPROCESS_BLUR_MAX_PAIRS][4];
    // Keep final-pass controls in the existing per-frame UBO rather than
    // expanding the already-full 128-byte push-constant block. The bloom
    // stages consume only offset_weight; the final shader consumes hdr too.
    float hdr[4];
    // x = enabled, y/z = clamped scene-luma range, w = adaptation alpha.
    float auto_exposure[4];
} vk_postprocess_blur_kernel_t;

_Static_assert(sizeof(vk_postprocess_blur_kernel_t) ==
                   (VK_POSTPROCESS_BLUR_MAX_PAIRS + 2) * 4 * sizeof(float),
               "post-process uniform layout must match Vulkan shaders");

typedef struct {
    vk_context_t *ctx;
    bool initialized;
    bool swapchain_ready;
    VkPipelineLayout pipeline_layout;
    VkPipeline final_pipeline;
    VkPipeline bloom_pipeline;
    VkPipeline dof_pipeline;
    VkPipeline crt_pipeline;
    VkPipeline auto_exposure_pipeline;
    VkRenderPass bloom_render_pass;
    VkDescriptorSetLayout blur_kernel_descriptor_set_layout;
    VkDescriptorPool blur_kernel_descriptor_pool;
    VkSampler auto_exposure_sampler;
    cvar_t *waterwarp;
    cvar_t *color_correction;
    cvar_t *color_brightness;
    cvar_t *color_contrast;
    cvar_t *color_saturation;
    cvar_t *color_tint;
    cvar_t *color_split_shadows;
    cvar_t *color_split_highlights;
    cvar_t *color_split_strength;
    cvar_t *color_split_balance;
    cvar_t *color_lut;
    cvar_t *color_lut_intensity;
    cvar_t *hdr;
    cvar_t *hdr_exposure;
    cvar_t *hdr_white;
    cvar_t *hdr_gamma;
    cvar_t *hdr_auto;
    cvar_t *hdr_auto_min;
    cvar_t *hdr_auto_max;
    cvar_t *hdr_auto_speed;
    cvar_t *bloom;
    cvar_t *bloom_iterations;
    cvar_t *bloom_levels;
    cvar_t *bloom_downscale;
    cvar_t *bloom_firefly;
    cvar_t *bloom_sigma;
    cvar_t *bloom_threshold;
    cvar_t *bloom_knee;
    cvar_t *bloom_intensity;
    cvar_t *bloom_saturation;
    cvar_t *bloom_scene_saturation;
    cvar_t *show_bloom;
    cvar_t *crt_mode;
    cvar_t *crt_bright_boost;
    cvar_t *crt_hard_pix;
    cvar_t *crt_hard_scan;
    cvar_t *crt_mask_dark;
    cvar_t *crt_mask_light;
    cvar_t *crt_scale_in_linear_gamma;
    cvar_t *crt_shadow_mask;
    cvar_t *dof;
    cvar_t *dof_focus_distance;
    cvar_t *dof_blur_range;
    bool waterwarp_active;
    bool color_active;
    bool split_active;
    bool lut_active;
    bool hdr_active;
    bool hdr_auto_active;
    bool bloom_requested;
    bool bloom_active;
    bool show_bloom_active;
    bool bloom_authored_emission;
    bool dof_requested;
    bool dof_active;
    bool crt_active;
    bool bloom_resources_dirty;
    bool bloom_supported;
    bool bloom_mips_supported;
    bool dof_resources_dirty;
    bool dof_supported;
    bool lut_valid;
    uint32_t descriptor_generation;
    qhandle_t lut_image;
    int lut_width;
    int lut_height;
    float lut_size;
    uint32_t bloom_width;
    uint32_t bloom_height;
    uint32_t bloom_mip_levels;
    uint32_t bloom_active_mip_levels;
    uint32_t dof_width;
    uint32_t dof_height;
    vk_postprocess_frame_resources_t frame_resources[VK_MAX_FRAMES_IN_FLIGHT];
    float tint[4];
    float split_shadow[4];
    float split_highlight[4];
    float hdr_controls[4];
    float auto_exposure_controls[4];
    vk_postprocess_push_t push;
    vk_postprocess_bloom_push_t bloom_push;
    vk_postprocess_crt_push_t crt_push;
    vk_postprocess_dof_push_t dof_push;
} vk_postprocess_state_t;

static vk_postprocess_state_t vk_postprocess;

static uint32_t VK_PostProcess_ActiveBloomMipLevels(void);
static void VK_PostProcess_UpdateDirectSceneKernelDescriptors(vk_context_t *ctx);

static vk_postprocess_frame_resources_t *VK_PostProcess_CurrentFrameResources(void)
{
    if (!vk_postprocess.ctx || !vk_postprocess.ctx->frame_count ||
        vk_postprocess.ctx->current_frame >= vk_postprocess.ctx->frame_count) {
        return NULL;
    }
    return &vk_postprocess.frame_resources[vk_postprocess.ctx->current_frame];
}

static VkImageView VK_PostProcess_CurrentSceneView(void)
{
    if (!vk_postprocess.ctx || !vk_postprocess.ctx->frame_count ||
        vk_postprocess.ctx->current_frame >= vk_postprocess.ctx->frame_count) {
        return VK_NULL_HANDLE;
    }
    vk_frame_context_t *frame =
        &vk_postprocess.ctx->frames[vk_postprocess.ctx->current_frame];
    return frame->linear_scene_copy_view
        ? frame->linear_scene_copy_view : frame->liquid_scene_view;
}

// HDR scene data must not be quantized while bloom or DOF samples it. The
// presentation image remains UNORM; only native post-process working targets
// follow the frame-slot float scene format.
static VkFormat VK_PostProcess_WorkingFormat(void)
{
    if (!vk_postprocess.ctx) {
        return VK_FORMAT_UNDEFINED;
    }
    return vk_postprocess.ctx->frames[0].linear_scene_image
        ? vk_postprocess.ctx->scene_format
        : vk_postprocess.ctx->swapchain.format;
}

static VkImageView VK_PostProcess_CurrentBloomEmissionView(void)
{
    if (!vk_postprocess.ctx || !vk_postprocess.ctx->frame_count ||
        vk_postprocess.ctx->current_frame >= vk_postprocess.ctx->frame_count) {
        return VK_NULL_HANDLE;
    }
    return vk_postprocess.ctx->frames[vk_postprocess.ctx->current_frame]
        .bloom_emission_view;
}

static VkImageView VK_PostProcess_CurrentDepthView(void)
{
    if (!vk_postprocess.ctx || !vk_postprocess.ctx->frame_count ||
        vk_postprocess.ctx->current_frame >= vk_postprocess.ctx->frame_count) {
        return VK_NULL_HANDLE;
    }
    return vk_postprocess.ctx->frames[vk_postprocess.ctx->current_frame]
        .depth_sample_view;
}

static bool VK_PostProcess_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }
    Com_SetLastError(va("Vulkan post-process %s failed: %d", what, (int)result));
    return false;
}

static uint32_t VK_PostProcess_FindMemoryType(VkPhysicalDevice physical_device,
                                              uint32_t type_filter,
                                              VkMemoryPropertyFlags properties);

static void VK_PostProcess_DestroyBlurKernelResources(void)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device) {
        return;
    }

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        if (frame->blur_kernel_mapped) {
            vkUnmapMemory(ctx->device, frame->blur_kernel_memory);
            frame->blur_kernel_mapped = NULL;
        }
        if (frame->blur_kernel_buffer) {
            vkDestroyBuffer(ctx->device, frame->blur_kernel_buffer, NULL);
            frame->blur_kernel_buffer = VK_NULL_HANDLE;
        }
        if (frame->blur_kernel_memory) {
            vkFreeMemory(ctx->device, frame->blur_kernel_memory, NULL);
            frame->blur_kernel_memory = VK_NULL_HANDLE;
        }
        frame->blur_kernel_descriptor_set = VK_NULL_HANDLE;
        frame->direct_scene_kernel_descriptor_set = VK_NULL_HANDLE;
        frame->auto_exposure_kernel_descriptor_set = VK_NULL_HANDLE;
        frame->blur_kernel_sigma = 0.0f;
        frame->blur_kernel_initialized = false;
    }
    if (vk_postprocess.blur_kernel_descriptor_pool) {
        vkDestroyDescriptorPool(ctx->device,
                                vk_postprocess.blur_kernel_descriptor_pool,
                                NULL);
        vk_postprocess.blur_kernel_descriptor_pool = VK_NULL_HANDLE;
    }
    if (vk_postprocess.blur_kernel_descriptor_set_layout) {
        vkDestroyDescriptorSetLayout(
            ctx->device, vk_postprocess.blur_kernel_descriptor_set_layout,
            NULL);
        vk_postprocess.blur_kernel_descriptor_set_layout = VK_NULL_HANDLE;
    }
    if (vk_postprocess.auto_exposure_sampler) {
        vkDestroySampler(ctx->device, vk_postprocess.auto_exposure_sampler,
                         NULL);
        vk_postprocess.auto_exposure_sampler = VK_NULL_HANDLE;
    }
}

static bool VK_PostProcess_CreateBlurKernelBuffer(
    vk_postprocess_frame_resources_t *frame)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device || !frame) {
        return false;
    }

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(vk_postprocess_blur_kernel_t),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_PostProcess_Check(vkCreateBuffer(ctx->device, &buffer_info, NULL,
                                              &frame->blur_kernel_buffer),
                              "vkCreateBuffer(blur kernel)")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(ctx->device, frame->blur_kernel_buffer,
                                  &requirements);
    uint32_t memory_type = VK_PostProcess_FindMemoryType(
        ctx->physical_device, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        Com_WPrintf("Vulkan post-process: host-visible blur-kernel memory is unavailable\n");
        return false;
    }

    VkMemoryAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!VK_PostProcess_Check(vkAllocateMemory(ctx->device, &allocation_info,
                                                NULL,
                                                &frame->blur_kernel_memory),
                              "vkAllocateMemory(blur kernel)")) {
        return false;
    }
    if (!VK_PostProcess_Check(vkBindBufferMemory(ctx->device,
                                                  frame->blur_kernel_buffer,
                                                  frame->blur_kernel_memory, 0),
                              "vkBindBufferMemory(blur kernel)")) {
        return false;
    }
    if (!VK_PostProcess_Check(vkMapMemory(ctx->device, frame->blur_kernel_memory,
                                           0, requirements.size, 0,
                                           &frame->blur_kernel_mapped),
                              "vkMapMemory(blur kernel)")) {
        return false;
    }
    return true;
}

static bool VK_PostProcess_CreateBlurKernelResources(void)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device) {
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
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
    if (!VK_PostProcess_Check(vkCreateDescriptorSetLayout(
                                  ctx->device, &layout_info, NULL,
                                  &vk_postprocess.blur_kernel_descriptor_set_layout),
                              "vkCreateDescriptorSetLayout(blur kernel)")) {
        return false;
    }

    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = VK_MAX_FRAMES_IN_FLIGHT * 3,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = VK_MAX_FRAMES_IN_FLIGHT * 3,
        },
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = VK_MAX_FRAMES_IN_FLIGHT * 3,
        .poolSizeCount = q_countof(pool_sizes),
        .pPoolSizes = pool_sizes,
    };
    if (!VK_PostProcess_Check(vkCreateDescriptorPool(
                                  ctx->device, &pool_info, NULL,
                                  &vk_postprocess.blur_kernel_descriptor_pool),
                              "vkCreateDescriptorPool(blur kernel)")) {
        VK_PostProcess_DestroyBlurKernelResources();
        return false;
    }

    VkDescriptorSetLayout layouts[VK_MAX_FRAMES_IN_FLIGHT * 3];
    for (uint32_t i = 0; i < q_countof(layouts); ++i) {
        layouts[i] = vk_postprocess.blur_kernel_descriptor_set_layout;
    }
    VkDescriptorSet descriptor_sets[VK_MAX_FRAMES_IN_FLIGHT * 3];
    VkDescriptorSetAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_postprocess.blur_kernel_descriptor_pool,
        .descriptorSetCount = q_countof(descriptor_sets),
        .pSetLayouts = layouts,
    };
    if (!VK_PostProcess_Check(vkAllocateDescriptorSets(ctx->device,
                                                        &allocation_info,
                                                        descriptor_sets),
                              "vkAllocateDescriptorSets(blur kernel)")) {
        VK_PostProcess_DestroyBlurKernelResources();
        return false;
    }

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        frame->blur_kernel_descriptor_set = descriptor_sets[i];
        frame->auto_exposure_kernel_descriptor_set =
            descriptor_sets[VK_MAX_FRAMES_IN_FLIGHT + i];
        frame->direct_scene_kernel_descriptor_set =
            descriptor_sets[VK_MAX_FRAMES_IN_FLIGHT * 2 + i];
        if (!VK_PostProcess_CreateBlurKernelBuffer(frame)) {
            VK_PostProcess_DestroyBlurKernelResources();
            return false;
        }

        VkDescriptorBufferInfo buffer_info = {
            .buffer = frame->blur_kernel_buffer,
            .range = sizeof(vk_postprocess_blur_kernel_t),
        };
        VkWriteDescriptorSet writes[3] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frame->blur_kernel_descriptor_set,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_info,
            },
        };
        writes[1] = writes[0];
        writes[1].dstSet = frame->auto_exposure_kernel_descriptor_set;
        writes[2] = writes[0];
        writes[2].dstSet = frame->direct_scene_kernel_descriptor_set;
        vkUpdateDescriptorSets(ctx->device, q_countof(writes), writes, 0, NULL);
    }
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
    };
    if (!VK_PostProcess_Check(vkCreateSampler(ctx->device, &sampler_info, NULL,
                                               &vk_postprocess.auto_exposure_sampler),
                              "vkCreateSampler(auto exposure)")) {
        VK_PostProcess_DestroyBlurKernelResources();
        return false;
    }
    VK_PostProcess_UpdateDirectSceneKernelDescriptors(ctx);
    return true;
}

static void VK_PostProcess_UpdateDirectSceneKernelDescriptors(vk_context_t *ctx)
{
    if (!ctx || !ctx->device || !vk_postprocess.auto_exposure_sampler) {
        return;
    }
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *scene_frame = &ctx->frames[i];
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        if (!scene_frame->linear_scene_view ||
            !frame->direct_scene_kernel_descriptor_set) {
            continue;
        }
        VkDescriptorImageInfo image_info = {
            .sampler = vk_postprocess.auto_exposure_sampler,
            .imageView = scene_frame->linear_scene_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frame->direct_scene_kernel_descriptor_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
        };
        vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
    }
}

static void VK_PostProcess_UpdateBlurKernel(float sigma)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!frame || !frame->blur_kernel_mapped) {
        return;
    }

    // This UBO is host-coherent and per frame-slot. HDR controls therefore
    // update without a descriptor rewrite or any additional allocation even
    // while the blur weights themselves stay cached.
    vk_postprocess_blur_kernel_t *mapped = frame->blur_kernel_mapped;
    memcpy(mapped->hdr, vk_postprocess.hdr_controls, sizeof(mapped->hdr));
    memcpy(mapped->auto_exposure, vk_postprocess.auto_exposure_controls,
           sizeof(mapped->auto_exposure));

    sigma = max(sigma, 0.5f);
    if (frame->blur_kernel_initialized &&
        fabsf(frame->blur_kernel_sigma - sigma) <= 0.00001f) {
        return;
    }

    vk_postprocess_blur_kernel_t kernel = { 0 };
    memcpy(kernel.hdr, vk_postprocess.hdr_controls, sizeof(kernel.hdr));
    memcpy(kernel.auto_exposure, vk_postprocess.auto_exposure_controls,
           sizeof(kernel.auto_exposure));
    const int radius = min((int)(sigma * 2.0f + 0.5f), 50);
    const float inverse_sigma_squared = 1.0f / (sigma * sigma);
    uint32_t pair = 0;
    for (int i = -radius; i <= radius &&
                       pair < VK_POSTPROCESS_BLUR_MAX_PAIRS;
         i += 2, ++pair) {
        const float weight0 = expf(-((float)i * (float)i) *
                                   inverse_sigma_squared);
        float weight = weight0;
        float offset = (float)i;
        if (i != radius) {
            const float next = (float)(i + 1);
            const float weight1 = expf(-(next * next) *
                                       inverse_sigma_squared);
            weight += weight1;
            offset += weight1 / max(weight, 1e-5f);
        }
        kernel.offset_weight[pair][0] = offset;
        kernel.offset_weight[pair][1] = weight;
    }
    memcpy(frame->blur_kernel_mapped, &kernel, sizeof(kernel));
    frame->blur_kernel_sigma = sigma;
    frame->blur_kernel_initialized = true;
}

static void VK_PostProcess_ParseColor(cvar_t *self, float output[4])
{
    color_t color;

    if (!SCR_ParseColor(self->string, &color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        color.u32 = COLOR_U32_WHITE;
    }

    output[0] = color.u8[0] / 255.0f;
    output[1] = color.u8[1] / 255.0f;
    output[2] = color.u8[2] / 255.0f;
    output[3] = 1.0f;
}

static void VK_PostProcess_ColorTintChanged(cvar_t *self)
{
    VK_PostProcess_ParseColor(self, vk_postprocess.tint);
}

static void VK_PostProcess_ColorSplitShadowsChanged(cvar_t *self)
{
    VK_PostProcess_ParseColor(self, vk_postprocess.split_shadow);
}

static void VK_PostProcess_ColorSplitHighlightsChanged(cvar_t *self)
{
    VK_PostProcess_ParseColor(self, vk_postprocess.split_highlight);
}

static void VK_PostProcess_DestroyExternalDescriptors(
    vk_postprocess_frame_resources_t *frame)
{
    if (!frame) {
        return;
    }
    VK_UI_DestroyExternalImageDescriptor(&frame->final_scene_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->bloom_scene_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->bloom_ping_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->bloom_pong_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->dof_scene_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->dof_ping_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->dof_pong_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->dof_composite_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->auto_exposure_descriptor_set);
    frame->descriptor_lut_active = false;
    frame->descriptor_bloom_active = false;
    frame->descriptor_show_bloom_active = false;
    frame->descriptor_bloom_emission_active = false;
    frame->descriptor_dof_active = false;
    frame->descriptor_hdr_auto_active = false;
    frame->descriptor_generation = 0;
}

static void VK_PostProcess_DestroyAllExternalDescriptors(void)
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_PostProcess_DestroyExternalDescriptors(&vk_postprocess.frame_resources[i]);
    }
}

static void VK_PostProcess_ResetColorLut(void)
{
    vk_postprocess.lut_image = 0;
    vk_postprocess.lut_width = 0;
    vk_postprocess.lut_height = 0;
    vk_postprocess.lut_size = 0.0f;
    vk_postprocess.lut_valid = false;
    vk_postprocess.descriptor_generation++;
    if (!vk_postprocess.descriptor_generation) {
        vk_postprocess.descriptor_generation = 1;
    }
}

static void VK_PostProcess_ColorLutChanged(cvar_t *self)
{
    VK_PostProcess_ResetColorLut();
    if (!self || !self->string[0]) {
        return;
    }

    qhandle_t image = VK_UI_RegisterImage(
        self->string, IT_PIC, IF_PERMANENT | IF_EXACT | IF_NO_COLOR_ADJUST);
    if (!image) {
        Com_WPrintf("Vulkan color LUT '%s' could not be loaded\n", self->string);
        return;
    }

    int width = 0;
    int height = 0;
    if (!VK_UI_GetPicSize(&width, &height, image) || width <= 0 || height <= 0) {
        Com_WPrintf("Vulkan color LUT '%s' has invalid dimensions\n", self->string);
        return;
    }

    int size = 0;
    if (width == height * height) {
        size = height;
    } else if (height == width * width) {
        size = width;
    }
    if (size <= 0) {
        Com_WPrintf("Vulkan color LUT '%s' expects NxN strip (got %dx%d)\n",
                    self->string, width, height);
        return;
    }

    vk_postprocess.lut_image = image;
    vk_postprocess.lut_width = width;
    vk_postprocess.lut_height = height;
    vk_postprocess.lut_size = (float)size;
    vk_postprocess.lut_valid = true;
}

static void VK_PostProcess_UpdateExternalDescriptors(void)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!frame) {
        return;
    }
    if (frame->descriptor_generation == vk_postprocess.descriptor_generation &&
        frame->descriptor_lut_active == vk_postprocess.lut_active &&
        frame->descriptor_bloom_active == vk_postprocess.bloom_active &&
        frame->descriptor_show_bloom_active ==
            vk_postprocess.show_bloom_active &&
        frame->descriptor_bloom_emission_active ==
            vk_postprocess.bloom_authored_emission &&
        frame->descriptor_dof_active == vk_postprocess.dof_active &&
        frame->descriptor_hdr_auto_active == vk_postprocess.hdr_auto_active) {
        return;
    }

    VK_PostProcess_DestroyExternalDescriptors(frame);
    VkImageView scene_view = VK_PostProcess_CurrentSceneView();
    if (!scene_view) {
        return;
    }
    // The final fragment module declares the auto-exposure sampler even when
    // the branch is disabled, so keep every frame-slot descriptor valid.
    if (frame->blur_kernel_descriptor_set &&
        vk_postprocess.auto_exposure_sampler) {
        VkDescriptorImageInfo image_info = {
            .sampler = vk_postprocess.auto_exposure_sampler,
            .imageView = scene_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frame->blur_kernel_descriptor_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
        };
        vkUpdateDescriptorSets(vk_postprocess.ctx->device, 1, &write, 0, NULL);
    }

    if (vk_postprocess.dof_active) {
        VkImageView depth_view = VK_PostProcess_CurrentDepthView();
        if (!depth_view || !frame->dof_ping.view || !frame->dof_pong.view ||
            !frame->dof_scene.view) {
            Com_WPrintf("Vulkan depth-aware DOF resources are unavailable\n");
            vk_postprocess.dof_active = false;
        } else {
            frame->dof_scene_descriptor_set =
                VK_UI_CreateExternalImageDescriptor(
                    scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            frame->dof_ping_descriptor_set =
                VK_UI_CreateExternalImageDescriptor(
                    frame->dof_ping.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            frame->dof_pong_descriptor_set =
                VK_UI_CreateExternalImageDescriptor(
                    frame->dof_pong.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            frame->dof_composite_descriptor_set =
                VK_UI_CreateExternalImageTripleDescriptor(
                    scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    frame->dof_ping.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    depth_view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            if (!frame->dof_scene_descriptor_set || !frame->dof_ping_descriptor_set ||
                !frame->dof_pong_descriptor_set || !frame->dof_composite_descriptor_set) {
                Com_WPrintf("Vulkan depth-aware DOF descriptors could not be allocated\n");
                VK_PostProcess_DestroyExternalDescriptors(frame);
                vk_postprocess.dof_active = false;
            }
        }
    }

    // This is the native counterpart to gl_showbloom: sample the completed
    // level-zero blurred image directly, without routing through OpenGL or
    // applying the final display transforms. render_view deliberately names
    // only mip zero, as GL's TEXNUM_PP_BLUR_0 does.
    VkImageView source_scene_view = vk_postprocess.show_bloom_active
        ? frame->bloom_ping.render_view
        : (vk_postprocess.dof_active ? frame->dof_scene.view : scene_view);
    VkImageView lut_view = source_scene_view;
    if (vk_postprocess.lut_active) {
        lut_view = VK_UI_GetImageView(vk_postprocess.lut_image);
        if (!lut_view) {
            Com_WPrintf("Vulkan color LUT image is unavailable\n");
            vk_postprocess.lut_valid = false;
            vk_postprocess.lut_active = false;
            lut_view = source_scene_view;
        }
    }

    VkImageView bloom_view = vk_postprocess.bloom_active && frame->bloom_ping.view
        ? frame->bloom_ping.view : source_scene_view;
    if (vk_postprocess.lut_active || vk_postprocess.bloom_active ||
        vk_postprocess.dof_active) {
        frame->final_scene_descriptor_set =
            VK_UI_CreateExternalImageTripleDescriptor(
                source_scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                lut_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                bloom_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->final_scene_descriptor_set) {
            Com_WPrintf("Vulkan post-process final descriptor could not be allocated\n");
            vk_postprocess.lut_active = false;
            vk_postprocess.bloom_active = false;
            vk_postprocess.dof_active = false;
            vk_postprocess.push.lut_params[0] = 0.0f;
            vk_postprocess.push.bloom_final[0] = 0.0f;
            vk_postprocess.push.bloom_final[1] = 1.0f;
            vk_postprocess.push.bloom_final[2] = 1.0f;
            vk_postprocess.push.bloom_final[3] = 0.0f;
            return;
        }
    }

    if (vk_postprocess.bloom_active) {
        VkImageView emission_view = vk_postprocess.bloom_authored_emission
            ? VK_PostProcess_CurrentBloomEmissionView() : VK_NULL_HANDLE;
        frame->bloom_scene_descriptor_set = emission_view
            ? VK_UI_CreateExternalImagePairDescriptor(
                  scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  emission_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            : VK_UI_CreateExternalImageDescriptor(
                  scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            frame->bloom_ping_descriptor_set =
                VK_UI_CreateExternalImageDescriptor(
                    frame->bloom_ping.render_view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frame->bloom_pong_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                frame->bloom_pong.view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->bloom_scene_descriptor_set ||
            !frame->bloom_ping_descriptor_set ||
            !frame->bloom_pong_descriptor_set) {
            Com_WPrintf("Vulkan bloom descriptors could not be allocated\n");
            VK_PostProcess_DestroyExternalDescriptors(frame);
            vk_postprocess.bloom_active = false;
            vk_postprocess.lut_active = false;
            vk_postprocess.push.lut_params[0] = 0.0f;
            vk_postprocess.push.bloom_final[0] = 0.0f;
            vk_postprocess.push.bloom_final[1] = 1.0f;
            vk_postprocess.push.bloom_final[2] = 1.0f;
            vk_postprocess.push.bloom_final[3] = 0.0f;
        }
    }

    if (vk_postprocess.hdr_auto_active) {
        vk_postprocess_bloom_image_t *previous =
            &frame->auto_exposure[frame->auto_exposure_index];
        if (!previous->view) {
            Com_WPrintf("Vulkan HDR auto exposure resources are unavailable\n");
            vk_postprocess.hdr_auto_active = false;
        } else {
            frame->auto_exposure_descriptor_set =
                VK_UI_CreateExternalImageTripleDescriptor(
                    scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    previous->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (!frame->auto_exposure_descriptor_set) {
                Com_WPrintf("Vulkan HDR auto exposure descriptor could not be allocated\n");
                vk_postprocess.hdr_auto_active = false;
            }
        }
    }

    frame->descriptor_lut_active = vk_postprocess.lut_active;
    frame->descriptor_bloom_active = vk_postprocess.bloom_active;
    frame->descriptor_show_bloom_active = vk_postprocess.show_bloom_active;
    frame->descriptor_bloom_emission_active =
        vk_postprocess.bloom_authored_emission;
    frame->descriptor_dof_active = vk_postprocess.dof_active;
    frame->descriptor_hdr_auto_active = vk_postprocess.hdr_auto_active;
    frame->descriptor_generation = vk_postprocess.descriptor_generation;
}

static uint32_t VK_PostProcess_FindMemoryType(VkPhysicalDevice physical_device,
                                               uint32_t type_filter,
                                               VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((type_filter & BIT(i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static void VK_PostProcess_DestroyBloomImage(vk_postprocess_bloom_image_t *image)
{
    if (!image || !vk_postprocess.ctx || !vk_postprocess.ctx->device) {
        return;
    }

    VkDevice device = vk_postprocess.ctx->device;
    if (image->framebuffer) {
        vkDestroyFramebuffer(device, image->framebuffer, NULL);
        image->framebuffer = VK_NULL_HANDLE;
    }
    if (image->view) {
        vkDestroyImageView(device, image->view, NULL);
        image->view = VK_NULL_HANDLE;
    }
    if (image->render_view) {
        vkDestroyImageView(device, image->render_view, NULL);
        image->render_view = VK_NULL_HANDLE;
    }
    if (image->image) {
        vkDestroyImage(device, image->image, NULL);
        image->image = VK_NULL_HANDLE;
    }
    if (image->memory) {
        vkFreeMemory(device, image->memory, NULL);
        image->memory = VK_NULL_HANDLE;
    }
    image->mip_levels = 0;
    image->initialized = false;
    image->mip_chain_initialized = false;
}

static void VK_PostProcess_DestroyBloomImages(void)
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        VK_PostProcess_DestroyBloomImage(&frame->bloom_ping);
        VK_PostProcess_DestroyBloomImage(&frame->bloom_pong);
    }
    vk_postprocess.bloom_width = 0;
    vk_postprocess.bloom_height = 0;
    vk_postprocess.bloom_mip_levels = 0;
    vk_postprocess.bloom_active_mip_levels = 0;
    vk_postprocess.bloom_mips_supported = false;
    vk_postprocess.bloom_supported = false;
}

static void VK_PostProcess_DestroyAutoExposureImages(void)
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        VK_PostProcess_DestroyBloomImage(&frame->auto_exposure[0]);
        VK_PostProcess_DestroyBloomImage(&frame->auto_exposure[1]);
        frame->auto_exposure_index = 0;
        frame->auto_exposure_valid = false;
    }
}

static void VK_PostProcess_DestroyDofImages(void)
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        VK_PostProcess_DestroyBloomImage(&frame->dof_ping);
        VK_PostProcess_DestroyBloomImage(&frame->dof_pong);
        VK_PostProcess_DestroyBloomImage(&frame->dof_scene);
    }
    vk_postprocess.dof_width = 0;
    vk_postprocess.dof_height = 0;
    vk_postprocess.dof_supported = false;
}

static uint32_t VK_PostProcess_BloomMipCount(uint32_t width, uint32_t height)
{
    uint32_t min_dimension = min(width, height);
    uint32_t mip_levels = 1;

    while (min_dimension > 1 &&
           mip_levels < VK_POSTPROCESS_BLOOM_MAX_LEVELS) {
        min_dimension >>= 1;
        mip_levels++;
    }
    return mip_levels;
}

static bool VK_PostProcess_CreateBloomImage(uint32_t width, uint32_t height,
                                            uint32_t mip_levels,
                                            vk_postprocess_bloom_image_t *image)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device || !image || !vk_postprocess.bloom_render_pass ||
        !width || !height || !mip_levels) {
        return false;
    }

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_PostProcess_WorkingFormat(),
        .extent = { width, height, 1 },
        .mipLevels = mip_levels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 (mip_levels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!VK_PostProcess_Check(vkCreateImage(ctx->device, &image_info, NULL,
                                            &image->image),
                              "vkCreateImage(bloom)")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(ctx->device, image->image, &requirements);
    uint32_t memory_type = VK_PostProcess_FindMemoryType(
        ctx->physical_device, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("Vulkan bloom: suitable image memory type not found");
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!VK_PostProcess_Check(vkAllocateMemory(ctx->device, &alloc_info, NULL,
                                                &image->memory),
                              "vkAllocateMemory(bloom)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }
    if (!VK_PostProcess_Check(vkBindImageMemory(ctx->device, image->image,
                                                 image->memory, 0),
                              "vkBindImageMemory(bloom)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_PostProcess_WorkingFormat(),
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = mip_levels,
            .layerCount = 1,
        },
    };
    if (!VK_PostProcess_Check(vkCreateImageView(ctx->device, &view_info, NULL,
                                                &image->view),
                              "vkCreateImageView(bloom)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }

    view_info.subresourceRange.levelCount = 1;
    if (!VK_PostProcess_Check(vkCreateImageView(ctx->device, &view_info, NULL,
                                                &image->render_view),
                              "vkCreateImageView(bloom render)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }

    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = vk_postprocess.bloom_render_pass,
        .attachmentCount = 1,
        .pAttachments = &image->render_view,
        .width = width,
        .height = height,
        .layers = 1,
    };
    if (!VK_PostProcess_Check(vkCreateFramebuffer(ctx->device,
                                                   &framebuffer_info, NULL,
                                                   &image->framebuffer),
                              "vkCreateFramebuffer(bloom)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }
    image->mip_levels = mip_levels;
    return true;
}

static bool VK_PostProcess_CreateBloomImages(uint32_t width, uint32_t height)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device || !width || !height) {
        return false;
    }

    VkFormatProperties format_properties;
    const VkFormat working_format = VK_PostProcess_WorkingFormat();
    vkGetPhysicalDeviceFormatProperties(ctx->physical_device, working_format,
                                        &format_properties);
    const VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((format_properties.optimalTilingFeatures & required) != required) {
        Com_WPrintf("Vulkan bloom: working format lacks sampled colour attachment support\n");
        return false;
    }

    const VkFormatFeatureFlags mip_required = required |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    const bool mip_supported =
        (format_properties.optimalTilingFeatures & mip_required) == mip_required;
    if (!mip_supported) {
        Com_WPrintf("Vulkan bloom: working format lacks linear blit support; "
                    "using the native base bloom level only.\n");
    }
    const uint32_t mip_levels = mip_supported
        ? VK_PostProcess_BloomMipCount(width, height) : 1;

    VK_PostProcess_DestroyBloomImages();
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        if (!VK_PostProcess_CreateBloomImage(width, height, mip_levels,
                                             &frame->bloom_ping) ||
            !VK_PostProcess_CreateBloomImage(width, height, 1,
                                             &frame->bloom_pong)) {
            VK_PostProcess_DestroyBloomImages();
            return false;
        }
    }
    if (!ctx->frame_count) {
        VK_PostProcess_DestroyBloomImages();
        return false;
    }

    vk_postprocess.bloom_width = width;
    vk_postprocess.bloom_height = height;
    vk_postprocess.bloom_mip_levels = mip_levels;
    vk_postprocess.bloom_mips_supported = mip_supported;
    vk_postprocess.bloom_supported = true;
    return true;
}

static bool VK_PostProcess_CreateAutoExposureImages(void)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device || ctx->linear_scene_mip_levels <= 1) {
        return false;
    }
    VK_PostProcess_DestroyAutoExposureImages();
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        if (!VK_PostProcess_CreateBloomImage(1, 1, 1,
                                             &frame->auto_exposure[0]) ||
            !VK_PostProcess_CreateBloomImage(1, 1, 1,
                                             &frame->auto_exposure[1])) {
            VK_PostProcess_DestroyAutoExposureImages();
            return false;
        }
    }
    return true;
}

static bool VK_PostProcess_CreateDofImages(uint32_t width, uint32_t height)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device || !width || !height ||
        !ctx->scene_extent.width || !ctx->scene_extent.height) {
        return false;
    }

    VkFormatProperties format_properties;
    const VkFormat working_format = VK_PostProcess_WorkingFormat();
    vkGetPhysicalDeviceFormatProperties(ctx->physical_device, working_format,
                                        &format_properties);
    const VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((format_properties.optimalTilingFeatures & required) != required) {
        Com_WPrintf("Vulkan depth-aware DOF: working format lacks sampled colour attachment support\n");
        return false;
    }

    VK_PostProcess_DestroyDofImages();
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        if (!VK_PostProcess_CreateBloomImage(width, height, 1,
                                             &frame->dof_ping) ||
            !VK_PostProcess_CreateBloomImage(width, height, 1,
                                             &frame->dof_pong) ||
            !VK_PostProcess_CreateBloomImage(ctx->scene_extent.width,
                                              ctx->scene_extent.height, 1,
                                              &frame->dof_scene)) {
            VK_PostProcess_DestroyDofImages();
            return false;
        }
    }

    vk_postprocess.dof_width = width;
    vk_postprocess.dof_height = height;
    vk_postprocess.dof_supported = true;
    return true;
}

static bool VK_PostProcess_CreateBloomRenderPass(vk_context_t *ctx)
{
    VkAttachmentDescription color_attachment = {
        .format = VK_PostProcess_WorkingFormat(),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference color_reference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_reference,
    };
    VkSubpassDependency dependencies[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        },
    };
    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = q_countof(dependencies),
        .pDependencies = dependencies,
    };
    return VK_PostProcess_Check(vkCreateRenderPass(ctx->device,
                                                    &render_pass_info, NULL,
                                                    &vk_postprocess.bloom_render_pass),
                                "vkCreateRenderPass(bloom)");
}

bool VK_PostProcess_Init(vk_context_t *ctx)
{
    memset(&vk_postprocess, 0, sizeof(vk_postprocess));
    if (!ctx) {
        Com_SetLastError("Vulkan post-process: context is missing");
        return false;
    }

    vk_postprocess.ctx = ctx;
    vk_postprocess.waterwarp = Cvar_Get("vk_waterwarp", "1", 0);
    vk_postprocess.color_correction = Cvar_Get("vk_color_correction", "1", CVAR_ARCHIVE);
    vk_postprocess.color_brightness = Cvar_Get("vk_color_brightness", "0.0", CVAR_ARCHIVE);
    vk_postprocess.color_contrast = Cvar_Get("vk_color_contrast", "1.0", CVAR_ARCHIVE);
    vk_postprocess.color_saturation = Cvar_Get("vk_color_saturation", "1.0", CVAR_ARCHIVE);
    vk_postprocess.color_tint = Cvar_Get("vk_color_tint", "white", CVAR_ARCHIVE);
    vk_postprocess.color_tint->changed = VK_PostProcess_ColorTintChanged;
    vk_postprocess.color_tint->generator = Com_Color_g;
    VK_PostProcess_ColorTintChanged(vk_postprocess.color_tint);
    vk_postprocess.color_split_shadows =
        Cvar_Get("vk_color_split_shadows", "white", CVAR_ARCHIVE);
    vk_postprocess.color_split_shadows->changed =
        VK_PostProcess_ColorSplitShadowsChanged;
    vk_postprocess.color_split_shadows->generator = Com_Color_g;
    VK_PostProcess_ColorSplitShadowsChanged(
        vk_postprocess.color_split_shadows);
    vk_postprocess.color_split_highlights =
        Cvar_Get("vk_color_split_highlights", "white", CVAR_ARCHIVE);
    vk_postprocess.color_split_highlights->changed =
        VK_PostProcess_ColorSplitHighlightsChanged;
    vk_postprocess.color_split_highlights->generator = Com_Color_g;
    VK_PostProcess_ColorSplitHighlightsChanged(
        vk_postprocess.color_split_highlights);
    vk_postprocess.color_split_strength =
        Cvar_Get("vk_color_split_strength", "0.0", CVAR_ARCHIVE);
    vk_postprocess.color_split_balance =
        Cvar_Get("vk_color_split_balance", "0.0", CVAR_ARCHIVE);
    vk_postprocess.color_lut = Cvar_Get("vk_color_lut", "", CVAR_ARCHIVE);
    vk_postprocess.color_lut->changed = VK_PostProcess_ColorLutChanged;
    vk_postprocess.color_lut_intensity =
        Cvar_Get("vk_color_lut_intensity", "1.0", CVAR_ARCHIVE);
    VK_PostProcess_ColorLutChanged(vk_postprocess.color_lut);
    // Float scene attachments are swapchain-sized resources. Restart the
    // renderer when this toggle changes so disabled HDR carries no VRAM cost.
    vk_postprocess.hdr = Cvar_Get("vk_hdr", "0",
                                  CVAR_ARCHIVE | CVAR_RENDERER);
    vk_postprocess.hdr_exposure =
        Cvar_Get("vk_hdr_exposure", "1.0", CVAR_ARCHIVE);
    vk_postprocess.hdr_white =
        Cvar_Get("vk_hdr_white", "1.0", CVAR_ARCHIVE);
    vk_postprocess.hdr_gamma =
        Cvar_Get("vk_hdr_gamma", "2.2", CVAR_ARCHIVE);
    // The mip chain and temporal images are allocated only when this is
    // enabled, so changing it deliberately takes the normal renderer restart.
    vk_postprocess.hdr_auto =
        Cvar_Get("vk_hdr_auto_exposure", "0", CVAR_ARCHIVE | CVAR_RENDERER);
    vk_postprocess.hdr_auto_min =
        Cvar_Get("vk_hdr_auto_min_luma", "0.05", CVAR_ARCHIVE);
    vk_postprocess.hdr_auto_max =
        Cvar_Get("vk_hdr_auto_max_luma", "4.0", CVAR_ARCHIVE);
    vk_postprocess.hdr_auto_speed =
        Cvar_Get("vk_hdr_auto_speed", "2.0", CVAR_ARCHIVE);
    vk_postprocess.bloom = Cvar_Get("vk_bloom", "1", CVAR_ARCHIVE);
    vk_postprocess.bloom_iterations =
        Cvar_Get("vk_bloom_iterations", "1", CVAR_ARCHIVE);
    vk_postprocess.bloom_levels =
        Cvar_Get("vk_bloom_levels", "1", CVAR_ARCHIVE);
    vk_postprocess.bloom_downscale =
        Cvar_Get("vk_bloom_downscale", "4", CVAR_ARCHIVE);
    vk_postprocess.bloom_firefly =
        Cvar_Get("vk_bloom_firefly", "10.0", CVAR_ARCHIVE);
    vk_postprocess.bloom_sigma =
        Cvar_Get("vk_bloom_sigma", "8.0", CVAR_ARCHIVE);
    vk_postprocess.bloom_threshold =
        Cvar_Get("vk_bloom_threshold", "1.0", CVAR_ARCHIVE);
    vk_postprocess.bloom_knee =
        Cvar_Get("vk_bloom_knee", "0.5", CVAR_ARCHIVE);
    vk_postprocess.bloom_intensity =
        Cvar_Get("vk_bloom_intensity", "0.8", CVAR_ARCHIVE);
    vk_postprocess.bloom_saturation =
        Cvar_Get("vk_bloom_saturation", "1.0", CVAR_ARCHIVE);
    vk_postprocess.bloom_scene_saturation =
        Cvar_Get("vk_bloom_scene_saturation", "1.0", CVAR_ARCHIVE);
    vk_postprocess.show_bloom =
        Cvar_Get("vk_showbloom", "0", CVAR_CHEAT);
    vk_postprocess.dof = Cvar_Get("r_dof", "1", CVAR_ARCHIVE | CVAR_LATCH);
    vk_postprocess.dof_focus_distance =
        Cvar_Get("r_dof_focus_distance", "16.0", CVAR_SERVERINFO);
    vk_postprocess.dof_blur_range =
        Cvar_Get("r_dof_blur_range", "0.0", CVAR_SERVERINFO);
    vk_postprocess.crt_mode = Cvar_Get("r_crtmode", "0", CVAR_ARCHIVE);
    vk_postprocess.crt_bright_boost =
        Cvar_Get("r_crt_brightboost", "1.5", CVAR_SERVERINFO);
    vk_postprocess.crt_hard_pix =
        Cvar_Get("r_crt_hard_pix", "-8.0", CVAR_SERVERINFO);
    vk_postprocess.crt_hard_scan =
        Cvar_Get("r_crt_hard_scan", "-8.0", CVAR_SERVERINFO);
    vk_postprocess.crt_mask_dark =
        Cvar_Get("r_crt_mask_dark", "0.5", CVAR_SERVERINFO);
    vk_postprocess.crt_mask_light =
        Cvar_Get("r_crt_mask_light", "1.5", CVAR_SERVERINFO);
    vk_postprocess.crt_scale_in_linear_gamma =
        Cvar_Get("r_crt_scale_in_linear_gamma", "1.0", CVAR_ARCHIVE);
    vk_postprocess.crt_shadow_mask =
        Cvar_Get("r_crt_shadow_mask", "0.0", CVAR_ARCHIVE);
    VkDescriptorSetLayout scene_set_layout = VK_UI_GetDescriptorSetLayout();
    if (!scene_set_layout) {
        Com_SetLastError("Vulkan post-process: UI descriptor layout is unavailable");
        return false;
    }
    if (!VK_PostProcess_CreateBlurKernelResources()) {
        Com_SetLastError("Vulkan post-process: blur-kernel resources are unavailable");
        return false;
    }

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(vk_postprocess_push_t),
    };
    VkDescriptorSetLayout set_layouts[] = {
        scene_set_layout,
        vk_postprocess.blur_kernel_descriptor_set_layout,
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = q_countof(set_layouts),
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    if (!VK_PostProcess_Check(vkCreatePipelineLayout(ctx->device, &layout_info, NULL,
                                                     &vk_postprocess.pipeline_layout),
                              "vkCreatePipelineLayout")) {
        VK_PostProcess_DestroyBlurKernelResources();
        return false;
    }
    vk_postprocess.initialized = true;
    return true;
}

void VK_PostProcess_DestroySwapchainResources(vk_context_t *ctx)
{
    (void)ctx;
    if (!vk_postprocess.initialized || !vk_postprocess.ctx ||
        !vk_postprocess.ctx->device) {
        return;
    }
    if (vk_postprocess.final_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.final_pipeline, NULL);
        vk_postprocess.final_pipeline = VK_NULL_HANDLE;
    }
    if (vk_postprocess.bloom_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.bloom_pipeline, NULL);
        vk_postprocess.bloom_pipeline = VK_NULL_HANDLE;
    }
    if (vk_postprocess.dof_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.dof_pipeline, NULL);
        vk_postprocess.dof_pipeline = VK_NULL_HANDLE;
    }
    if (vk_postprocess.crt_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.crt_pipeline, NULL);
        vk_postprocess.crt_pipeline = VK_NULL_HANDLE;
    }
    if (vk_postprocess.auto_exposure_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.auto_exposure_pipeline, NULL);
        vk_postprocess.auto_exposure_pipeline = VK_NULL_HANDLE;
    }
    VK_PostProcess_DestroyAllExternalDescriptors();
    VK_PostProcess_DestroyAutoExposureImages();
    VK_PostProcess_DestroyBloomImages();
    VK_PostProcess_DestroyDofImages();
    if (vk_postprocess.bloom_render_pass) {
        vkDestroyRenderPass(vk_postprocess.ctx->device,
                            vk_postprocess.bloom_render_pass, NULL);
        vk_postprocess.bloom_render_pass = VK_NULL_HANDLE;
    }
    vk_postprocess.descriptor_generation++;
    if (!vk_postprocess.descriptor_generation) {
        vk_postprocess.descriptor_generation = 1;
    }
    vk_postprocess.bloom_resources_dirty = true;
    vk_postprocess.bloom_active = false;
    vk_postprocess.dof_resources_dirty = true;
    vk_postprocess.dof_active = false;
    vk_postprocess.swapchain_ready = false;
}

void VK_PostProcess_RefreshSceneResources(vk_context_t *ctx)
{
    if (!vk_postprocess.initialized || !ctx ||
        ctx != vk_postprocess.ctx || !ctx->device) {
        return;
    }

    // Submitted frames were retired by the caller before old scene views are
    // destroyed. The dependent descriptors can therefore be released even
    // though they formerly named those views.
    VK_PostProcess_DestroyAllExternalDescriptors();
    vk_postprocess.bloom_resources_dirty = true;
    vk_postprocess.dof_resources_dirty = true;
    vk_postprocess.bloom_active = false;
    vk_postprocess.bloom_active_mip_levels = 0;
    vk_postprocess.dof_active = false;
    vk_postprocess.descriptor_generation++;
    if (!vk_postprocess.descriptor_generation) {
        vk_postprocess.descriptor_generation = 1;
    }

    // This persistent descriptor set backs the direct HDR presentation path;
    // unlike the external descriptor family above it is updated in place.
    VK_PostProcess_UpdateDirectSceneKernelDescriptors(ctx);
}

bool VK_PostProcess_CreateSwapchainResources(vk_context_t *ctx)
{
    if (!vk_postprocess.initialized || !ctx ||
        !ctx->presentation_load_render_pass) {
        return false;
    }
    VK_PostProcess_DestroySwapchainResources(ctx);

    VkShaderModule vert_shader = VK_NULL_HANDLE;
    VkShaderModule frag_shader = VK_NULL_HANDLE;
    VkShaderModule bloom_frag_shader = VK_NULL_HANDLE;
    VkShaderModule dof_frag_shader = VK_NULL_HANDLE;
    VkShaderModule crt_frag_shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vert_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_postprocess_vert_spv_size,
        .pCode = vk_postprocess_vert_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device, &vert_info, NULL,
                                                   &vert_shader),
                              "vkCreateShaderModule(final postprocess vert)")) {
        return false;
    }
    VkShaderModuleCreateInfo frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_postprocess_frag_spv_size,
        .pCode = vk_postprocess_frag_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device, &frag_info, NULL,
                                                   &frag_shader),
                              "vkCreateShaderModule(final postprocess frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        return false;
    }
    VkShaderModuleCreateInfo bloom_frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_bloom_frag_spv_size,
        .pCode = vk_bloom_frag_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device,
                                                   &bloom_frag_info, NULL,
                                                   &bloom_frag_shader),
                              "vkCreateShaderModule(bloom frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        vkDestroyShaderModule(ctx->device, frag_shader, NULL);
        return false;
    }
    if (!VK_PostProcess_CreateBloomRenderPass(ctx)) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        vkDestroyShaderModule(ctx->device, frag_shader, NULL);
        vkDestroyShaderModule(ctx->device, bloom_frag_shader, NULL);
        return false;
    }
    VkShaderModuleCreateInfo dof_frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_dof_frag_spv_size,
        .pCode = vk_dof_frag_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device, &dof_frag_info,
                                                   NULL, &dof_frag_shader),
                              "vkCreateShaderModule(DOF frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        vkDestroyShaderModule(ctx->device, frag_shader, NULL);
        vkDestroyShaderModule(ctx->device, bloom_frag_shader, NULL);
        VK_PostProcess_DestroySwapchainResources(ctx);
        return false;
    }
    VkShaderModuleCreateInfo crt_frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_crt_frag_spv_size,
        .pCode = vk_crt_frag_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device, &crt_frag_info,
                                                   NULL, &crt_frag_shader),
                              "vkCreateShaderModule(CRT frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        vkDestroyShaderModule(ctx->device, frag_shader, NULL);
        vkDestroyShaderModule(ctx->device, bloom_frag_shader, NULL);
        vkDestroyShaderModule(ctx->device, dof_frag_shader, NULL);
        VK_PostProcess_DestroySwapchainResources(ctx);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
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
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
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
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_FALSE,
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
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk_postprocess.pipeline_layout,
        .renderPass = ctx->presentation_load_render_pass,
        .subpass = 0,
    };
    bool created = VK_PostProcess_Check(
        vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                  &pipeline_info, NULL,
                                  &vk_postprocess.final_pipeline),
        "vkCreateGraphicsPipelines(final postprocess)");
    if (created) {
        pipeline_info.renderPass = vk_postprocess.bloom_render_pass;
        stages[1].module = frag_shader;
        created = VK_PostProcess_Check(
            vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                      &pipeline_info, NULL,
                                      &vk_postprocess.auto_exposure_pipeline),
            "vkCreateGraphicsPipelines(auto exposure)");
    }
    if (created) {
        pipeline_info.renderPass = vk_postprocess.bloom_render_pass;
        stages[1].module = bloom_frag_shader;
        created = VK_PostProcess_Check(
            vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                      &pipeline_info, NULL,
                                      &vk_postprocess.bloom_pipeline),
            "vkCreateGraphicsPipelines(bloom)");
    }
    if (created) {
        pipeline_info.renderPass = vk_postprocess.bloom_render_pass;
        stages[1].module = dof_frag_shader;
        created = VK_PostProcess_Check(
            vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                      &pipeline_info, NULL,
                                      &vk_postprocess.dof_pipeline),
            "vkCreateGraphicsPipelines(DOF)");
    }
    if (created) {
        pipeline_info.renderPass = ctx->presentation_load_render_pass;
        stages[1].module = crt_frag_shader;
        created = VK_PostProcess_Check(
            vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                      &pipeline_info, NULL,
                                      &vk_postprocess.crt_pipeline),
            "vkCreateGraphicsPipelines(CRT)");
    }
    vkDestroyShaderModule(ctx->device, vert_shader, NULL);
    vkDestroyShaderModule(ctx->device, frag_shader, NULL);
    vkDestroyShaderModule(ctx->device, bloom_frag_shader, NULL);
    vkDestroyShaderModule(ctx->device, dof_frag_shader, NULL);
    vkDestroyShaderModule(ctx->device, crt_frag_shader, NULL);
    if (!created) {
        VK_PostProcess_DestroySwapchainResources(ctx);
        return false;
    }
    // Linear scene image views are created with the swapchain, after the
    // persistent post-process descriptor sets. Populate the direct path now.
    VK_PostProcess_UpdateDirectSceneKernelDescriptors(ctx);
    vk_postprocess.bloom_resources_dirty = true;
    vk_postprocess.descriptor_generation++;
    if (!vk_postprocess.descriptor_generation) {
        vk_postprocess.descriptor_generation = 1;
    }
    if (ctx->frames[0].linear_scene_copy_image && vk_postprocess.hdr_auto &&
        vk_postprocess.hdr_auto->integer &&
        !VK_PostProcess_CreateAutoExposureImages()) {
        Com_WPrintf("Vulkan HDR auto exposure resources are unavailable; "
                    "using static exposure.\n");
    }
    vk_postprocess.swapchain_ready = true;
    return true;
}

void VK_PostProcess_Shutdown(vk_context_t *ctx)
{
    if (!vk_postprocess.initialized) {
        return;
    }
    if (!ctx) {
        ctx = vk_postprocess.ctx;
    }
    VK_PostProcess_DestroySwapchainResources(ctx);
    if (ctx && ctx->device && vk_postprocess.pipeline_layout) {
        vkDestroyPipelineLayout(ctx->device, vk_postprocess.pipeline_layout, NULL);
    }
    VK_PostProcess_DestroyBlurKernelResources();
    memset(&vk_postprocess, 0, sizeof(vk_postprocess));
}

void VK_PostProcess_RenderFrame(const refdef_t *fd)
{
    vk_postprocess.waterwarp_active = fd &&
        (fd->rdflags & RDF_UNDERWATER) &&
        (!vk_postprocess.waterwarp || vk_postprocess.waterwarp->integer != 0);

    memset(&vk_postprocess.push, 0, sizeof(vk_postprocess.push));
    vk_postprocess.push.time = fd ? fd->time : 0.0f;
    vk_postprocess.push.waterwarp = vk_postprocess.waterwarp_active ? 1.0f : 0.0f;
    vk_postprocess.push.output_size[0] = vk_postprocess.ctx &&
        vk_postprocess.ctx->swapchain.extent.width
        ? (float)vk_postprocess.ctx->swapchain.extent.width
        : (fd && fd->width ? (float)fd->width : 1.0f);
    vk_postprocess.push.output_size[1] = vk_postprocess.ctx &&
        vk_postprocess.ctx->swapchain.extent.height
        ? (float)vk_postprocess.ctx->swapchain.extent.height
        : (fd && fd->height ? (float)fd->height : 1.0f);

    memset(&vk_postprocess.dof_push, 0, sizeof(vk_postprocess.dof_push));
    vk_postprocess.dof_push.rect[2] = 1.0f;
    vk_postprocess.dof_push.rect[3] = 1.0f;
    vk_postprocess.dof_requested = fd && vk_postprocess.dof &&
        vk_postprocess.dof->integer != 0 &&
        !(fd->rdflags & RDF_NOWORLDMODEL) && fd->dof_strength > 0.0f;
    if (vk_postprocess.dof_requested) {
        float zfar = 8192.0f;
        const bsp_t *bsp = VK_World_GetBsp();
        if (bsp && bsp->numnodes > 0) {
            vec3_t extents;
            VectorSubtract(bsp->nodes[0].maxs, bsp->nodes[0].mins, extents);
            zfar = max(2048.0f, VectorLength(extents) * 4.0f);
        }
        renderer_view_push_t view_push;
        R_BuildViewPush(fd, 4.0f, zfar, &view_push);
        vk_postprocess.dof_push.params[0] =
            vk_postprocess.dof_focus_distance
                ? vk_postprocess.dof_focus_distance->value : 16.0f;
        vk_postprocess.dof_push.params[1] =
            vk_postprocess.dof_blur_range
                ? vk_postprocess.dof_blur_range->value : 0.0f;
        vk_postprocess.dof_push.params[2] = Q_clipf(fd->dof_strength, 0.0f, 1.0f);
        vk_postprocess.dof_push.params[3] = view_push.proj[10];
        vk_postprocess.dof_push.projection[0] = view_push.proj[14];

        if (fd->dof_rect_enabled) {
            const float output_width = max(vk_postprocess.push.output_size[0], 1.0f);
            const float output_height = max(vk_postprocess.push.output_size[1], 1.0f);
            float left = max((float)fd->dof_rect.left, 0.0f);
            float top = max((float)fd->dof_rect.top, 0.0f);
            float right = min((float)fd->dof_rect.right, output_width);
            float bottom = min((float)fd->dof_rect.bottom, output_height);
            if (right <= left || bottom <= top) {
                vk_postprocess.dof_push.rect[0] = 1.0f;
                vk_postprocess.dof_push.rect[1] = 1.0f;
                vk_postprocess.dof_push.rect[2] = 0.0f;
                vk_postprocess.dof_push.rect[3] = 0.0f;
            } else {
                // cgame supplies top-origin pixels, while the native
                // post-process fragment coordinates are bottom-origin.
                // Reverse the vertical limits before vk_dof.frag rebases its
                // samples to the clipped full-quad domain.
                vk_postprocess.dof_push.rect[0] = left / output_width;
                vk_postprocess.dof_push.rect[1] = 1.0f - bottom / output_height;
                vk_postprocess.dof_push.rect[2] = right / output_width;
                vk_postprocess.dof_push.rect[3] = 1.0f - top / output_height;
            }
        }
    }

    const bool correction_enabled = vk_postprocess.color_correction &&
        vk_postprocess.color_correction->integer != 0;
    vk_postprocess.push.brightness = correction_enabled
        ? Cvar_ClampValue(vk_postprocess.color_brightness, -1.0f, 1.0f) : 0.0f;
    vk_postprocess.push.contrast = correction_enabled
        ? Cvar_ClampValue(vk_postprocess.color_contrast, 0.0f, 4.0f) : 1.0f;
    vk_postprocess.push.saturation = correction_enabled
        ? Cvar_ClampValue(vk_postprocess.color_saturation, 0.0f, 4.0f) : 1.0f;
    memcpy(vk_postprocess.push.tint, vk_postprocess.tint,
           sizeof(vk_postprocess.push.tint));

    vk_postprocess.color_active = correction_enabled &&
        (fabsf(vk_postprocess.push.brightness) > 0.0001f ||
         fabsf(vk_postprocess.push.contrast - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.saturation - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.tint[0] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.tint[1] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.tint[2] - 1.0f) > 0.0001f);
    vk_postprocess.push.color_enabled = vk_postprocess.color_active ? 1.0f : 0.0f;

    vk_postprocess.push.split_params[0] = Cvar_ClampValue(
        vk_postprocess.color_split_strength, 0.0f, 1.0f);
    vk_postprocess.push.split_params[1] = Cvar_ClampValue(
        vk_postprocess.color_split_balance, -1.0f, 1.0f);
    memcpy(vk_postprocess.push.split_shadow, vk_postprocess.split_shadow,
           sizeof(vk_postprocess.push.split_shadow));
    memcpy(vk_postprocess.push.split_highlight, vk_postprocess.split_highlight,
           sizeof(vk_postprocess.push.split_highlight));

    vk_postprocess.split_active =
        vk_postprocess.push.split_params[0] > 0.0001f &&
        (fabsf(vk_postprocess.push.split_shadow[0] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_shadow[1] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_shadow[2] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_highlight[0] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_highlight[1] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_highlight[2] - 1.0f) > 0.0001f);

    vk_postprocess.push.lut_params[0] = vk_postprocess.lut_valid
        ? Cvar_ClampValue(vk_postprocess.color_lut_intensity, 0.0f, 1.0f)
        : 0.0f;
    vk_postprocess.push.lut_params[1] = vk_postprocess.lut_size;
    vk_postprocess.push.lut_params[2] = vk_postprocess.lut_width > 0
        ? 1.0f / (float)vk_postprocess.lut_width : 0.0f;
    vk_postprocess.push.lut_params[3] = vk_postprocess.lut_height > 0
        ? 1.0f / (float)vk_postprocess.lut_height : 0.0f;
    vk_postprocess.lut_active = vk_postprocess.push.lut_params[0] > 0.0001f;

    // Match OpenGL's static controls and let native float-scene mip reduction
    // replace exposure only when the explicitly requested temporal path is
    // fully available for this frame slot.
    vk_postprocess.hdr_active = vk_postprocess.hdr &&
        vk_postprocess.hdr->integer != 0;
    vk_postprocess.hdr_controls[0] = vk_postprocess.hdr_active
        ? Cvar_ClampValue(vk_postprocess.hdr_exposure, 0.0f, 10.0f) : 1.0f;
    vk_postprocess.hdr_controls[1] = vk_postprocess.hdr_active
        ? Cvar_ClampValue(vk_postprocess.hdr_white, 0.1f, 20.0f) : 1.0f;
    vk_postprocess.hdr_controls[2] = vk_postprocess.hdr_active
        ? Cvar_ClampValue(vk_postprocess.hdr_gamma, 1.0f, 3.0f) : 1.0f;
    vk_postprocess.hdr_controls[3] = vk_postprocess.hdr_active ? 1.0f : 0.0f;
    vk_postprocess.hdr_auto_active = vk_postprocess.hdr_active &&
        vk_postprocess.hdr_auto && vk_postprocess.hdr_auto->integer != 0 &&
        vk_postprocess.ctx && vk_postprocess.ctx->linear_scene_mip_levels > 1 &&
        vk_postprocess.auto_exposure_pipeline &&
        VK_PostProcess_CurrentFrameResources() &&
        VK_PostProcess_CurrentFrameResources()->auto_exposure[0].image &&
        VK_PostProcess_CurrentFrameResources()->auto_exposure[1].image;
    const float hdr_auto_min = Cvar_ClampValue(vk_postprocess.hdr_auto_min,
                                                0.0001f, 10000.0f);
    const float hdr_auto_max = Cvar_ClampValue(vk_postprocess.hdr_auto_max,
                                                hdr_auto_min, 10000.0f);
    const float hdr_auto_speed = Cvar_ClampValue(vk_postprocess.hdr_auto_speed,
                                                  0.0f, 60.0f);
    const float hdr_auto_alpha = vk_postprocess.hdr_auto_active
        ? (hdr_auto_speed <= 0.0f ? 1.0f :
           1.0f - expf(-hdr_auto_speed * max(fd ? fd->frametime : 0.0f, 0.0f)))
        : 0.0f;
    vk_postprocess.auto_exposure_controls[0] =
        vk_postprocess.hdr_auto_active ? 1.0f : 0.0f;
    vk_postprocess.auto_exposure_controls[1] = hdr_auto_min;
    vk_postprocess.auto_exposure_controls[2] = hdr_auto_max;
    vk_postprocess.auto_exposure_controls[3] = hdr_auto_alpha;

    const int bloom_downscale = Cvar_ClampInteger(
        vk_postprocess.bloom_downscale, 1, 8);
    const float base_height = vk_postprocess.ctx &&
        vk_postprocess.ctx->swapchain.extent.height
        ? (float)vk_postprocess.ctx->swapchain.extent.height
        : (fd && fd->height ? (float)fd->height : 1.0f);
    vk_postprocess.bloom_push.params[0] = Cvar_ClampValue(
        vk_postprocess.bloom_threshold, 0.0f, 10.0f);
    vk_postprocess.bloom_push.params[1] = Cvar_ClampValue(
        vk_postprocess.bloom_knee, 0.0f, 1.0f);
    vk_postprocess.bloom_push.params[2] = Cvar_ClampValue(
        vk_postprocess.bloom_firefly, 0.0f, 1000.0f);
    vk_postprocess.bloom_push.params[3] = Cvar_ClampValue(
        vk_postprocess.bloom_sigma, 1.0f, 25.0f) * base_height / 2160.0f *
        4.0f / (float)bloom_downscale;
    vk_postprocess.bloom_push.params[3] =
        max(vk_postprocess.bloom_push.params[3], 0.5f);
    VK_PostProcess_UpdateBlurKernel(vk_postprocess.bloom_push.params[3]);
    vk_postprocess.bloom_requested = fd &&
        !(fd->rdflags & RDF_NOWORLDMODEL) && vk_postprocess.bloom &&
        vk_postprocess.bloom->integer != 0;
    vk_postprocess.bloom_authored_emission = false;

    vk_postprocess.crt_active = vk_postprocess.crt_mode &&
        vk_postprocess.crt_mode->integer != 0;
    vk_postprocess.crt_push.params[0] = vk_postprocess.crt_hard_pix
        ? min(vk_postprocess.crt_hard_pix->value, 0.0f) : -8.0f;
    vk_postprocess.crt_push.params[1] = vk_postprocess.crt_hard_scan
        ? min(vk_postprocess.crt_hard_scan->value, 0.0f) : -8.0f;
    vk_postprocess.crt_push.params[2] = vk_postprocess.crt_bright_boost
        ? max(vk_postprocess.crt_bright_boost->value, 0.0f) : 1.5f;
    vk_postprocess.crt_push.params[3] = vk_postprocess.crt_scale_in_linear_gamma
        ? Cvar_ClampValue(vk_postprocess.crt_scale_in_linear_gamma, 0.0f, 1.0f)
        : 1.0f;
    vk_postprocess.crt_push.params2[0] = vk_postprocess.crt_mask_dark
        ? max(vk_postprocess.crt_mask_dark->value, 0.0f) : 0.5f;
    vk_postprocess.crt_push.params2[1] = vk_postprocess.crt_mask_light
        ? max(vk_postprocess.crt_mask_light->value, 0.0f) : 1.5f;
    vk_postprocess.crt_push.params2[2] = vk_postprocess.crt_shadow_mask
        ? Cvar_ClampValue(vk_postprocess.crt_shadow_mask, 0.0f, 4.0f) : 0.0f;
    // Vulkan currently has no dynamic render scale, so the source and output
    // scanline grids share one pixel scale. Keep this explicit for the future
    // resolution-scaling path rather than baking it into the shader.
    vk_postprocess.crt_push.params2[3] = 1.0f;
    vk_postprocess.crt_push.texel[0] = 1.0f /
        max(vk_postprocess.push.output_size[0], 1.0f);
    vk_postprocess.crt_push.texel[1] = 1.0f /
        max(vk_postprocess.push.output_size[1], 1.0f);
    vk_postprocess.crt_push.texel[2] = vk_postprocess.push.output_size[0];
    vk_postprocess.crt_push.texel[3] = vk_postprocess.push.output_size[1];
}

void VK_PostProcess_SetBloomAuthoredEmission(bool active)
{
    vk_postprocess.bloom_authored_emission =
        vk_postprocess.bloom_requested && active;
}

bool VK_PostProcess_NeedsSafeResourceUpdate(void)
{
    if (!vk_postprocess.initialized || !vk_postprocess.swapchain_ready ||
        !VK_PostProcess_CurrentSceneView()) {
        return false;
    }

    bool bloom_resources_need_rebuild = false;
    bool dof_resources_need_rebuild = false;
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (vk_postprocess.bloom_requested) {
        const int downscale = Cvar_ClampInteger(vk_postprocess.bloom_downscale,
                                                 1, 8);
        uint32_t width = max(1u, vk_postprocess.ctx->scene_extent.width /
                                  (uint32_t)downscale);
        uint32_t height = max(1u, vk_postprocess.ctx->scene_extent.height /
                                   (uint32_t)downscale);
        bloom_resources_need_rebuild = !frame || vk_postprocess.bloom_resources_dirty ||
            !frame->bloom_ping.image || !frame->bloom_pong.image ||
            vk_postprocess.bloom_width != width ||
            vk_postprocess.bloom_height != height;
    }
    if (vk_postprocess.dof_requested) {
        uint32_t width = max(1u, vk_postprocess.ctx->scene_extent.width / 4u);
        uint32_t height = max(1u, vk_postprocess.ctx->scene_extent.height / 4u);
        dof_resources_need_rebuild = !frame || vk_postprocess.dof_resources_dirty ||
            !frame->dof_ping.image || !frame->dof_pong.image ||
            !frame->dof_scene.image || vk_postprocess.dof_width != width ||
            vk_postprocess.dof_height != height;
    }
    return bloom_resources_need_rebuild || dof_resources_need_rebuild;
}

void VK_PostProcess_PrepareFrame(void)
{
    if (!vk_postprocess.initialized || !vk_postprocess.swapchain_ready ||
        !VK_PostProcess_CurrentSceneView()) {
        return;
    }

    bool previous_bloom_active = vk_postprocess.bloom_active;
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!frame) {
        return;
    }
    if (vk_postprocess.bloom_requested) {
        const int downscale = Cvar_ClampInteger(vk_postprocess.bloom_downscale,
                                                 1, 8);
        uint32_t width = max(1u, vk_postprocess.ctx->scene_extent.width /
                                  (uint32_t)downscale);
        uint32_t height = max(1u, vk_postprocess.ctx->scene_extent.height /
                                   (uint32_t)downscale);
        if (vk_postprocess.bloom_resources_dirty ||
            !frame->bloom_ping.image || !frame->bloom_pong.image ||
            vk_postprocess.bloom_width != width ||
            vk_postprocess.bloom_height != height) {
            VK_PostProcess_DestroyAllExternalDescriptors();
            VK_PostProcess_CreateBloomImages(width, height);
            vk_postprocess.descriptor_generation++;
            if (!vk_postprocess.descriptor_generation) {
                vk_postprocess.descriptor_generation = 1;
            }
            vk_postprocess.bloom_resources_dirty = false;
        }
    }

    if (vk_postprocess.dof_requested) {
        uint32_t width = max(1u, vk_postprocess.ctx->scene_extent.width / 4u);
        uint32_t height = max(1u, vk_postprocess.ctx->scene_extent.height / 4u);
        if (vk_postprocess.dof_resources_dirty || !frame->dof_ping.image ||
            !frame->dof_pong.image || !frame->dof_scene.image ||
            vk_postprocess.dof_width != width || vk_postprocess.dof_height != height) {
            VK_PostProcess_DestroyAllExternalDescriptors();
            vk_postprocess.dof_supported =
                VK_PostProcess_CreateDofImages(width, height);
            vk_postprocess.descriptor_generation++;
            if (!vk_postprocess.descriptor_generation) {
                vk_postprocess.descriptor_generation = 1;
            }
            vk_postprocess.dof_resources_dirty = false;
        }
    }

    vk_postprocess.bloom_active = vk_postprocess.bloom_requested &&
        vk_postprocess.bloom_supported && frame->bloom_ping.image &&
        frame->bloom_pong.image;
    vk_postprocess.show_bloom_active = vk_postprocess.bloom_active &&
        vk_postprocess.show_bloom && vk_postprocess.show_bloom->integer != 0 &&
        frame->bloom_ping.render_view != VK_NULL_HANDLE;
    vk_postprocess.bloom_active_mip_levels = vk_postprocess.bloom_active
        ? VK_PostProcess_ActiveBloomMipLevels() : 0;
    vk_postprocess.bloom_push.aux[1] = vk_postprocess.bloom_active &&
        vk_postprocess.bloom_authored_emission &&
        VK_PostProcess_CurrentBloomEmissionView() != VK_NULL_HANDLE ? 1.0f : 0.0f;
    vk_postprocess.push.bloom_final[0] = vk_postprocess.bloom_active
        ? Cvar_ClampValue(vk_postprocess.bloom_intensity, 0.0f, 10.0f)
        : 0.0f;
    vk_postprocess.push.bloom_final[1] = vk_postprocess.bloom_active
        ? Cvar_ClampValue(vk_postprocess.bloom_scene_saturation, 0.0f, 4.0f)
        : 1.0f;
    vk_postprocess.push.bloom_final[2] = vk_postprocess.bloom_active
        ? Cvar_ClampValue(vk_postprocess.bloom_saturation, 0.0f, 4.0f)
        : 1.0f;
    vk_postprocess.push.bloom_final[3] = vk_postprocess.show_bloom_active
        ? -2.0f : (float)vk_postprocess.bloom_active_mip_levels;
    vk_postprocess.dof_active = vk_postprocess.dof_requested &&
        vk_postprocess.dof_supported && frame->dof_ping.image &&
        frame->dof_pong.image && frame->dof_scene.image &&
        VK_PostProcess_CurrentDepthView() != VK_NULL_HANDLE;
    (void)previous_bloom_active;
    VK_PostProcess_UpdateExternalDescriptors();
}

static void VK_PostProcess_ImageBarrier(VkCommandBuffer cmd, VkImage image,
                                        VkImageLayout old_layout,
                                        VkImageLayout new_layout,
                                        VkAccessFlags src_access,
                                        VkAccessFlags dst_access,
                                        VkPipelineStageFlags src_stage,
                                        VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL,
                         1, &barrier);
}

static void VK_PostProcess_ImageBarrierRange(
    VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
    VkImageLayout new_layout, VkAccessFlags src_access,
    VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage, uint32_t base_mip_level,
    uint32_t level_count)
{
    if (!level_count) {
        return;
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = base_mip_level,
            .levelCount = level_count,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL,
                         1, &barrier);
}

static uint32_t VK_PostProcess_ActiveBloomMipLevels(void)
{
    if (!vk_postprocess.bloom_mips_supported ||
        vk_postprocess.bloom_mip_levels <= 1) {
        return 1;
    }
    return min((uint32_t)Cvar_ClampInteger(vk_postprocess.bloom_levels, 1,
                                           VK_POSTPROCESS_BLOOM_MAX_LEVELS),
               vk_postprocess.bloom_mip_levels);
}

static void VK_PostProcess_GenerateBloomMips(
    VkCommandBuffer cmd, vk_postprocess_bloom_image_t *image,
    uint32_t active_levels)
{
    if (!cmd || !image || !image->image || image->mip_levels <= 1) {
        return;
    }

    active_levels = min(max(active_levels, 1u), image->mip_levels);

    // The final descriptor spans the whole chain. Make every unused level a
    // valid shader-read subresource before the descriptor is consumed, while
    // retaining only the requested levels for filtering work.
    if (!image->mip_chain_initialized) {
        VK_PostProcess_ImageBarrierRange(
            cmd, image->image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 1, image->mip_levels - 1);
        image->mip_chain_initialized = true;
    }
    if (active_levels <= 1) {
        return;
    }

    VK_PostProcess_ImageBarrierRange(
        cmd, image->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1);

    for (uint32_t level = 1; level < active_levels; ++level) {
        VK_PostProcess_ImageBarrierRange(
            cmd, image->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, level, 1);

        const uint32_t src_width = max(1u, vk_postprocess.bloom_width >> (level - 1));
        const uint32_t src_height = max(1u, vk_postprocess.bloom_height >> (level - 1));
        const uint32_t dst_width = max(1u, vk_postprocess.bloom_width >> level);
        const uint32_t dst_height = max(1u, vk_postprocess.bloom_height >> level);
        VkImageBlit blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = level - 1,
                .layerCount = 1,
            },
            .srcOffsets = { { 0, 0, 0 }, { (int32_t)src_width, (int32_t)src_height, 1 } },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = level,
                .layerCount = 1,
            },
            .dstOffsets = { { 0, 0, 0 }, { (int32_t)dst_width, (int32_t)dst_height, 1 } },
        };
        vkCmdBlitImage(cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        VK_PostProcess_ImageBarrierRange(
            cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, level, 1);
    }

    VK_PostProcess_ImageBarrierRange(
        cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, active_levels);
}

static void VK_PostProcess_RecordBloomPass(
    VkCommandBuffer cmd, vk_postprocess_bloom_image_t *target,
    VkDescriptorSet descriptor_set, float mode, uint32_t width,
    uint32_t height, const vk_postprocess_bloom_push_t *source_push)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!cmd || !target || !target->image || !target->framebuffer ||
        !descriptor_set || !vk_postprocess.bloom_pipeline || !source_push ||
        !frame || !frame->blur_kernel_descriptor_set || !width || !height) {
        return;
    }

    VK_PostProcess_ImageBarrier(
        cmd, target->image,
        target->initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        target->initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        target->initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk_postprocess.bloom_render_pass,
        .framebuffer = target->framebuffer,
        .renderArea = {
            .extent = {
                .width = width,
                .height = height,
            },
        },
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)height,
        .width = (float)width,
        .height = -(float)height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .extent = {
            .width = width,
            .height = height,
        },
    };
    vk_postprocess_bloom_push_t push = *source_push;
    push.output_size[0] = (float)width;
    push.output_size[1] = (float)height;
    push.aux[0] = mode;
    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.bloom_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkDescriptorSet descriptor_sets[] = {
        descriptor_set,
        frame->blur_kernel_descriptor_set,
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0,
                            q_countof(descriptor_sets), descriptor_sets,
                            0, NULL);
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_POSTPROCESS, 3, 0);
    vkCmdEndRenderPass(cmd);
    target->initialized = true;
}

void VK_PostProcess_RecordBloom(VkCommandBuffer cmd)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!vk_postprocess.bloom_active || !cmd || !frame ||
        !frame->bloom_scene_descriptor_set ||
        !frame->bloom_ping_descriptor_set || !frame->bloom_pong_descriptor_set) {
        return;
    }

    VK_PostProcess_RecordBloomPass(
        cmd, &frame->bloom_ping,
        frame->bloom_scene_descriptor_set, VK_BLOOM_MODE_PREFILTER,
        vk_postprocess.bloom_width, vk_postprocess.bloom_height,
        &vk_postprocess.bloom_push);

    const int passes = Cvar_ClampInteger(vk_postprocess.bloom_iterations,
                                         1, 8) * 2;
    for (int i = 0; i < passes; i++) {
        if (i & 1) {
            VK_PostProcess_RecordBloomPass(
                cmd, &frame->bloom_ping,
                frame->bloom_pong_descriptor_set, VK_BLOOM_MODE_BLUR_Y,
                vk_postprocess.bloom_width, vk_postprocess.bloom_height,
                &vk_postprocess.bloom_push);
        } else {
            VK_PostProcess_RecordBloomPass(
                cmd, &frame->bloom_pong,
                frame->bloom_ping_descriptor_set, VK_BLOOM_MODE_BLUR_X,
                vk_postprocess.bloom_width, vk_postprocess.bloom_height,
                &vk_postprocess.bloom_push);
        }
    }
    VK_PostProcess_GenerateBloomMips(cmd, &frame->bloom_ping,
                                     vk_postprocess.bloom_active_mip_levels);
}

void VK_PostProcess_RecordAutoExposure(VkCommandBuffer cmd)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!vk_postprocess.hdr_auto_active || !cmd || !frame ||
        !frame->auto_exposure_descriptor_set ||
        !frame->blur_kernel_descriptor_set ||
        !frame->auto_exposure_kernel_descriptor_set ||
        !vk_postprocess.auto_exposure_pipeline ||
        !vk_postprocess.auto_exposure_sampler) {
        return;
    }

    if (!frame->auto_exposure_valid) {
        const VkClearColorValue identity = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } };
        for (uint32_t i = 0; i < q_countof(frame->auto_exposure); ++i) {
            vk_postprocess_bloom_image_t *image = &frame->auto_exposure[i];
            VK_PostProcess_ImageBarrier(
                cmd, image->image, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkImageSubresourceRange range = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            };
            vkCmdClearColorImage(cmd, image->image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &identity, 1, &range);
            VK_PostProcess_ImageBarrier(
                cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
    }

    const uint32_t target_index = 1 - frame->auto_exposure_index;
    vk_postprocess_bloom_image_t *previous =
        &frame->auto_exposure[frame->auto_exposure_index];
    vk_postprocess_bloom_image_t *target =
        &frame->auto_exposure[target_index];
    VkDescriptorImageInfo previous_info = {
        .sampler = vk_postprocess.auto_exposure_sampler,
        .imageView = previous->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet previous_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = frame->auto_exposure_kernel_descriptor_set,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &previous_info,
    };
    vkUpdateDescriptorSets(vk_postprocess.ctx->device, 1, &previous_write,
                           0, NULL);
    VK_PostProcess_ImageBarrier(
        cmd, target->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkViewport viewport = { .width = 1.0f, .height = 1.0f,
                            .minDepth = 0.0f, .maxDepth = 1.0f };
    VkRect2D scissor = { .extent = { 1, 1 } };
    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk_postprocess.bloom_render_pass,
        .framebuffer = target->framebuffer,
        .renderArea = { .extent = { 1, 1 } },
    };
    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.auto_exposure_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkDescriptorSet descriptor_sets[] = {
        frame->auto_exposure_descriptor_set,
        frame->auto_exposure_kernel_descriptor_set,
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0,
                            q_countof(descriptor_sets), descriptor_sets,
                            0, NULL);
    vk_postprocess_push_t push = vk_postprocess.push;
    push.bloom_final[3] = -1.0f;
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    if (!frame->auto_exposure_valid && frame->blur_kernel_mapped) {
        ((vk_postprocess_blur_kernel_t *)frame->blur_kernel_mapped)
            ->auto_exposure[3] = 1.0f;
    }
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    VkDescriptorImageInfo exposure_info = {
        .sampler = vk_postprocess.auto_exposure_sampler,
        .imageView = target->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = frame->blur_kernel_descriptor_set,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &exposure_info,
    };
    vkUpdateDescriptorSets(vk_postprocess.ctx->device, 1, &write, 0, NULL);
    frame->auto_exposure_index = target_index;
    frame->auto_exposure_valid = true;
    // The source binding follows the ping-pong index; refresh it when this
    // slot is next reused after its fence has completed.
    frame->descriptor_hdr_auto_active = false;
    VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_POSTPROCESS, 3, 0);
}

static void VK_PostProcess_RecordDofComposite(
    VkCommandBuffer cmd, vk_postprocess_bloom_image_t *target,
    VkDescriptorSet descriptor_set)
{
    if (!cmd || !target || !target->image || !target->framebuffer ||
        !descriptor_set || !vk_postprocess.dof_pipeline ||
        !vk_postprocess.ctx) {
        return;
    }

    VK_PostProcess_ImageBarrier(
        cmd, target->image,
        target->initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        target->initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        target->initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VkExtent2D extent = vk_postprocess.ctx->scene_extent;
    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk_postprocess.bloom_render_pass,
        .framebuffer = target->framebuffer,
        .renderArea = {
            .extent = extent,
        },
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)extent.height,
        .width = (float)extent.width,
        .height = -(float)extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .extent = extent,
    };
    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.dof_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0, 1,
                            &descriptor_set, 0, NULL);
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(vk_postprocess.dof_push),
                       &vk_postprocess.dof_push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_POSTPROCESS, 3, 0);
    vkCmdEndRenderPass(cmd);
    target->initialized = true;
}

static bool VK_PostProcess_RecordDofBlur(
    VkCommandBuffer cmd, vk_postprocess_frame_resources_t *frame)
{
    if (!vk_postprocess.dof_active || !cmd || !frame ||
        !frame->dof_scene_descriptor_set || !frame->dof_ping_descriptor_set ||
        !frame->dof_pong_descriptor_set || !frame->dof_composite_descriptor_set) {
        return false;
    }

    // OpenGL's native path uses a quarter-resolution downsample followed by
    // four alternating Gaussian passes (two X/Y pairs) before its depth-aware
    // composite.
    vk_postprocess_bloom_push_t blur_push = { 0 };
    const float base_height = vk_postprocess.ctx->scene_extent.height
        ? (float)vk_postprocess.ctx->scene_extent.height : 1.0f;
    const int blur_downscale = Cvar_ClampInteger(
        vk_postprocess.bloom_downscale, 1, 8);
    // GL's shared Gaussian programs scale sigma by output height and its
    // configured bloom downscale even when they service quarter-res DOF.
    blur_push.params[3] = Cvar_ClampValue(vk_postprocess.bloom_sigma,
                                          1.0f, 25.0f) *
        base_height / 2160.0f * 4.0f / (float)blur_downscale;
    blur_push.params[3] = max(blur_push.params[3], 0.5f);
    VK_PostProcess_RecordBloomPass(
        cmd, &frame->dof_ping, frame->dof_scene_descriptor_set,
        VK_BLOOM_MODE_COPY, vk_postprocess.dof_width,
        vk_postprocess.dof_height, &blur_push);
    for (int i = 0; i < 4; i++) {
        if (i & 1) {
            VK_PostProcess_RecordBloomPass(
                cmd, &frame->dof_ping, frame->dof_pong_descriptor_set,
                VK_BLOOM_MODE_BLUR_Y, vk_postprocess.dof_width,
                vk_postprocess.dof_height, &blur_push);
        } else {
            VK_PostProcess_RecordBloomPass(
                cmd, &frame->dof_pong, frame->dof_ping_descriptor_set,
                VK_BLOOM_MODE_BLUR_X, vk_postprocess.dof_width,
                vk_postprocess.dof_height, &blur_push);
        }
    }
    return true;
}

void VK_PostProcess_RecordDof(VkCommandBuffer cmd)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!VK_PostProcess_RecordDofBlur(cmd, frame)) {
        return;
    }
    VK_PostProcess_RecordDofComposite(cmd, &frame->dof_scene,
                                      frame->dof_composite_descriptor_set);
}

bool VK_PostProcess_UsesCompositePass(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        ((VK_PostProcess_CurrentFrameResources() &&
          vk_postprocess.ctx->frames[vk_postprocess.ctx->current_frame]
              .linear_scene_copy_view) ||
         vk_postprocess.waterwarp_active || vk_postprocess.color_active ||
         vk_postprocess.split_active || vk_postprocess.lut_active ||
         vk_postprocess.hdr_active || vk_postprocess.bloom_active ||
         vk_postprocess.dof_active) &&
        vk_postprocess.final_pipeline;
}

bool VK_PostProcess_AllowsScaledSceneBlit(void)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        frame && vk_postprocess.ctx &&
        VK_PostProcess_UsesCompositePass() &&
        vk_postprocess.ctx->frames[vk_postprocess.ctx->current_frame]
            .linear_scene_copy_view &&
        !vk_postprocess.waterwarp_active && !vk_postprocess.color_active &&
        !vk_postprocess.split_active && !vk_postprocess.lut_active &&
        !vk_postprocess.hdr_active && !vk_postprocess.bloom_active &&
        !vk_postprocess.dof_active && !vk_postprocess.crt_active;
}

bool VK_PostProcess_UsesBloom(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        vk_postprocess.bloom_active && vk_postprocess.bloom_pipeline;
}

bool VK_PostProcess_UsesBloomEmission(void)
{
    return VK_PostProcess_UsesBloom() && vk_postprocess.bloom_authored_emission;
}

bool VK_PostProcess_UsesAutoExposure(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        vk_postprocess.hdr_auto_active &&
        vk_postprocess.auto_exposure_pipeline;
}

bool VK_PostProcess_RequiresSceneCopy(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        (VK_PostProcess_UsesAutoExposure() || vk_postprocess.bloom_active ||
         vk_postprocess.dof_active || vk_postprocess.lut_active);
}

bool VK_PostProcess_UsesCrtPass(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        vk_postprocess.crt_active && vk_postprocess.crt_pipeline;
}

bool VK_PostProcess_UsesFinalPass(void)
{
    return VK_PostProcess_UsesCompositePass() || VK_PostProcess_UsesCrtPass();
}

bool VK_PostProcess_UsesDof(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        vk_postprocess.dof_active && vk_postprocess.dof_pipeline;
}

void VK_PostProcess_RecordFinal(VkCommandBuffer cmd, const VkExtent2D *extent,
                                VkDescriptorSet scene_descriptor_set,
                                bool direct_linear_scene)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    VkDescriptorSet descriptor_set = frame && frame->final_scene_descriptor_set
        ? frame->final_scene_descriptor_set : scene_descriptor_set;
    VkDescriptorSet kernel_descriptor_set = frame && direct_linear_scene
        ? frame->direct_scene_kernel_descriptor_set
        : (frame ? frame->blur_kernel_descriptor_set : VK_NULL_HANDLE);
    if (!VK_PostProcess_UsesCompositePass() || !cmd || !extent ||
        !descriptor_set || !frame || !kernel_descriptor_set) {
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.final_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkDescriptorSet descriptor_sets[] = {
        descriptor_set,
        kernel_descriptor_set,
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0,
                            q_countof(descriptor_sets), descriptor_sets,
                            0, NULL);
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(vk_postprocess.push),
                       &vk_postprocess.push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_POSTPROCESS, 3, 0);
}

void VK_PostProcess_RecordCrt(VkCommandBuffer cmd, const VkExtent2D *extent,
                              VkDescriptorSet scene_descriptor_set)
{
    if (!VK_PostProcess_UsesCrtPass() || !cmd || !extent ||
        !scene_descriptor_set) {
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.crt_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0, 1,
                            &scene_descriptor_set, 0, NULL);
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(vk_postprocess.crt_push),
                       &vk_postprocess.crt_push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_POSTPROCESS, 3, 0);
}
