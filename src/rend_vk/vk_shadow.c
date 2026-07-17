/*
Copyright (C) 2026
*/

#include "vk_shadow.h"
#include "vk_debug.h"

#include "vk_entity.h"
#include "vk_world.h"
#include "vk_world_spv.h"
#include "renderer/view_setup.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define VK_SHADOW_DEFAULT_STRENGTH 1.0f
#define VK_SHADOW_DEFAULT_RECEIVER_BIAS 0.0008f
#define VK_SHADOW_CONE_RECEIVER_BIAS 0.00004f
#define VK_SHADOW_DEFAULT_MIN_BIAS 0.00005f
#define VK_SHADOW_CONE_MIN_BIAS 0.000005f
#define VK_SHADOW_CONE_NORMAL_OFFSET 0.05f
#define VK_SHADOW_CONE_DEPTH_BIAS 0.25f
#define VK_SHADOW_INITIAL_VERTEX_CAPACITY 4096u
#define VK_SHADOW_STREAM_BUFFER_MIN_BYTES (64u * 1024u)

// EVSM warp exponent shared by the moment pass (push constant) and the world
// receiver (shadow_moment_tuning UBO member). Moments are stored as (w, w*w)
// in a 16-bit float format, so exp(2*e) must stay below the fp16 max of
// 65504, which caps e at ~5.54.
#define VK_SHADOW_EVSM_EXPONENT 5.4f
#define VK_SHADOW_MOMENT_MIN_VARIANCE 0.00002f

enum {
    VK_FOG_GLOBAL = BIT(0),
    VK_FOG_HEIGHT = BIT(1),
    VK_FOG_SKY = BIT(2),
};

typedef struct {
    VkDescriptorSet descriptor_set;
    VkBuffer uniform_buffer;
    VkDeviceMemory uniform_memory;
    void *uniform_mapped;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer vertex_staging_buffer;
    VkDeviceMemory vertex_staging_memory;
    void *vertex_staging_mapped;
    size_t vertex_buffer_bytes;
    size_t vertex_upload_bytes;
    bool vertex_upload_recorded;
} vk_shadow_frame_resources_t;

typedef struct {
    float pos[3];
    float uv[2];
    float lm_uv[2];
    uint32_t color;
    float base_uv[2];
    uint8_t base_alpha;
    uint8_t flags;
    uint16_t reserved;
    float normal[3];
} vk_shadow_vertex_t;

typedef struct {
    renderer_view_push_t view;
    float filter;
    float evsm_exponent;
    float pad[2];
} vk_shadow_push_t;

typedef struct {
    uint32_t page;
    uint32_t first_vertex;
    uint32_t vertex_count;
    vk_shadow_push_t push;
    float slope_bias;
    float bias_scale;
    shadow_storage_family_t storage_family;
} vk_shadow_job_t;

typedef struct {
    float matrix[16];
    float params[4];
} vk_shadow_uniform_page_t;

typedef struct {
    float position_radius[4];
    float color_intensity[4];
    float cone[4];
    float shadow_pages0[4];
    float shadow_pages1[4];
} vk_shadow_uniform_dlight_t;

typedef struct {
    float global[4];
    float sun[4];
    // x = VSM/EVSM minimum variance, y = EVSM warp exponent, z = legacy
    // texture intensity, w = global r_fullbright state. Must match the
    // ShadowPages block in src/rend_vk/shaders/vk_world_shadow.frag.
    float moment_tuning[4];
    // x = r_glowmap_intensity; reserved components deliberately keep the
    // receiver UBO 16-byte aligned for both world and entity shaders.
    float glowmap_tuning[4];
    float dlight_count[4];
    float view_origin[4];
    // rgb + global density / 64, height fog start/end colours + distances,
    // then height density/falloff, sky factor and native fog feature bits.
    float fog_color_density[4];
    float heightfog_start[4];
    float heightfog_end[4];
    float fog_params[4];
    vk_shadow_uniform_page_t pages[VK_SHADOW_MAX_PAGES];
    vk_shadow_uniform_dlight_t dlights[MAX_DLIGHTS];
} vk_shadow_uniform_t;

typedef struct {
    int pages[SHADOW_FRONTEND_POINT_FACES];
    int page_count;
    shadow_view_type_t view_type;
} vk_shadow_light_pages_t;

typedef struct {
    vk_context_t *ctx;
    shadow_frontend_policy_t policy;
    bool initialized;
    bool frame_active;
    bool resources_ok;
    bool reallocated_this_frame;
    bool reallocated_last_frame;
    bool layout_initialized;

    VkFormat depth_format;
    VkFormat moment_format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView array_view;
    VkSampler sampler;
    VkSampler compare_sampler;
    VkImage moment_image;
    VkDeviceMemory moment_memory;
    VkImageView moment_array_view;
    VkSampler moment_sampler;
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkPipeline moment_pipeline;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    vk_shadow_frame_resources_t frame_resources[VK_MAX_FRAMES_IN_FLIGHT];
    VkImageView layer_views[VK_SHADOW_MAX_PAGES];
    VkImageView moment_layer_views[VK_SHADOW_MAX_PAGES];
    VkFramebuffer framebuffers[VK_SHADOW_MAX_PAGES];
    VkFramebuffer moment_framebuffers[VK_SHADOW_MAX_PAGES];
    VkImageLayout layout;
    VkImageLayout moment_layout;
    int resolution;
    int mip_levels;
    bool moment_mips_supported;
    int max_active_page;
    bool sun_active;
    shadow_storage_family_t storage_family;
    int world_faces_considered;
    int world_faces_submitted;

    vk_shadow_vertex_t *vertices;
    uint32_t vertex_count;
    uint32_t vertex_capacity;
    vk_shadow_job_t jobs[VK_SHADOW_MAX_PAGES];
    uint32_t job_count;
    vk_shadow_uniform_t uniform;
    vk_shadow_light_pages_t lights[MAX_DLIGHTS];
} vk_shadow_state_t;

static vk_shadow_state_t vk_shadow;
static cvar_t *vk_fog;

typedef struct {
    vec3_t mins;
    vec3_t maxs;
} vk_shadow_face_bounds_t;

// World face bounds are immutable per map; caching them avoids walking every
// face's surfedges again for every shadow view every frame.
typedef struct {
    const bsp_t *bsp;
    uint32_t checksum;
    int numfaces;
    vk_shadow_face_bounds_t *bounds;
} vk_shadow_world_cache_t;

static vk_shadow_world_cache_t vk_shadow_world_cache;

static vk_shadow_frame_resources_t *VK_Shadow_CurrentFrameResources(void)
{
    if (!vk_shadow.ctx || !vk_shadow.ctx->frame_count ||
        vk_shadow.ctx->current_frame >= vk_shadow.ctx->frame_count) {
        return NULL;
    }
    return &vk_shadow.frame_resources[vk_shadow.ctx->current_frame];
}

static void VK_Shadow_FreeWorldCache(void)
{
    free(vk_shadow_world_cache.bounds);
    memset(&vk_shadow_world_cache, 0, sizeof(vk_shadow_world_cache));
}

static float VK_Shadow_ViewReceiverBias(const shadow_view_desc_t *view)
{
    float bias_scale = max(vk_shadow.policy.bias_scale, 0.0f);
    if (bias_scale <= 0.0f) {
        return 0.0f;
    }

    bool cone = view && view->view_type == SHADOW_VIEW_CONE;
    float base = cone ? VK_SHADOW_CONE_RECEIVER_BIAS
                      : VK_SHADOW_DEFAULT_RECEIVER_BIAS;
    float min_bias = cone ? VK_SHADOW_CONE_MIN_BIAS
                          : VK_SHADOW_DEFAULT_MIN_BIAS;
    return max(min_bias, bias_scale * base);
}

static float VK_Shadow_ViewNormalOffset(const shadow_view_desc_t *view)
{
    float normal_offset = max(vk_shadow.policy.normal_offset, 0.0f);
    if (view && view->view_type == SHADOW_VIEW_CONE) {
        normal_offset = min(normal_offset, VK_SHADOW_CONE_NORMAL_OFFSET);
    }
    return normal_offset;
}

static float VK_Shadow_ViewSlopeBias(const shadow_view_desc_t *view)
{
    float slope_bias = max(vk_shadow.policy.slope_bias, 0.0f);
    if (view && view->view_type == SHADOW_VIEW_CONE) {
        slope_bias = min(slope_bias, VK_SHADOW_CONE_DEPTH_BIAS);
    }
    return slope_bias;
}

static float VK_Shadow_ViewConstantBias(const shadow_view_desc_t *view)
{
    float constant_bias = max(vk_shadow.policy.bias_scale, 0.0f);
    if (view && view->view_type == SHADOW_VIEW_CONE) {
        constant_bias = min(constant_bias, VK_SHADOW_CONE_DEPTH_BIAS);
    }
    return constant_bias;
}

static void VK_Shadow_UploadUniform(void)
{
    vk_shadow_frame_resources_t *frame = VK_Shadow_CurrentFrameResources();
    if (frame && frame->uniform_mapped) {
        memcpy(frame->uniform_mapped, &vk_shadow.uniform,
               sizeof(vk_shadow.uniform));
    }
}

static bool VK_Shadow_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }
    Com_EPrintf("%s failed: %d\n", what ? what : "Vulkan shadow operation",
                result);
    return false;
}

static bool VK_Shadow_ArrayBytes(size_t count, size_t item_size, size_t *out_size,
                                 const char *what)
{
    if (!out_size || (item_size && count > SIZE_MAX / item_size)) {
        Com_EPrintf("Vulkan shadow: %s allocation size overflow\n",
                    what ? what : "buffer");
        return false;
    }

    *out_size = count * item_size;
    return true;
}

static bool VK_Shadow_AddCount(uint32_t value, uint32_t delta, uint32_t *out_value,
                               const char *what)
{
    if (!out_value || value > UINT32_MAX - delta) {
        Com_EPrintf("Vulkan shadow: %s count overflow\n",
                    what ? what : "vertex");
        return false;
    }

    *out_value = value + delta;
    return true;
}

static bool VK_Shadow_GrowCapacity(uint32_t current, uint32_t needed,
                                   uint32_t *out_capacity, const char *what)
{
    if (!out_capacity) {
        return false;
    }

    uint32_t new_capacity = current ? current : VK_SHADOW_INITIAL_VERTEX_CAPACITY;
    while (new_capacity < needed) {
        if (new_capacity > UINT32_MAX / 2u) {
            Com_EPrintf("Vulkan shadow: %s capacity overflow\n",
                        what ? what : "vertex");
            return false;
        }
        new_capacity *= 2u;
    }

    *out_capacity = new_capacity;
    return true;
}

static bool VK_Shadow_ValidStorageFamily(shadow_storage_family_t storage)
{
    return storage == SHADOW_STORAGE_DEPTH_COMPARE ||
           storage == SHADOW_STORAGE_MOMENT;
}

static bool VK_Shadow_ValidView(const shadow_view_desc_t *view)
{
    return view &&
           view->page.index < VK_SHADOW_MAX_PAGES &&
           view->resolution > 0 &&
           VK_Shadow_ValidStorageFamily(view->storage_family);
}

static uint32_t VK_Shadow_FindMemoryType(uint32_t type_filter,
                                         VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(vk_shadow.ctx->physical_device,
                                        &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static int VK_Shadow_ClampResolution(int requested)
{
    requested = (int)Q_clipf((float)requested, 64.0f,
                             (float)VK_SHADOW_MAX_RESOLUTION);
    int resolution = 64;
    while (resolution < requested) {
        resolution <<= 1;
    }
    return min(resolution, VK_SHADOW_MAX_RESOLUTION);
}

static bool VK_Shadow_DepthFormatUsable(VkFormat format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(vk_shadow.ctx->physical_device,
                                        format, &props);
    VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (props.optimalTilingFeatures & required) == required;
}

static VkFormat VK_Shadow_ChooseDepthFormat(void)
{
    static const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
    };

    for (size_t i = 0; i < q_countof(candidates); i++) {
        if (VK_Shadow_DepthFormatUsable(candidates[i])) {
            return candidates[i];
        }
    }
    return VK_FORMAT_UNDEFINED;
}

static bool VK_Shadow_ColorFormatUsable(VkFormat format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(vk_shadow.ctx->physical_device,
                                        format, &props);
    VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (props.optimalTilingFeatures & required) == required;
}

static bool VK_Shadow_ColorFormatMipBlitUsable(VkFormat format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(vk_shadow.ctx->physical_device,
                                        format, &props);
    VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (props.optimalTilingFeatures & required) == required;
}

static VkFormat VK_Shadow_ChooseMomentFormat(void)
{
    static const VkFormat candidates[] = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
    };

    for (size_t i = 0; i < q_countof(candidates); i++) {
        if (VK_Shadow_ColorFormatUsable(candidates[i])) {
            return candidates[i];
        }
    }
    return VK_FORMAT_UNDEFINED;
}

static int VK_Shadow_MipLevels(int resolution)
{
    if (!vk_shadow.moment_mips_supported) {
        return 1;
    }

    int levels = 1;
    while (resolution > 1) {
        resolution >>= 1;
        levels++;
    }
    return levels;
}

static void VK_Shadow_MultiplyMatrix(const mat4_t a, const mat4_t b,
                                     mat4_t out)
{
    mat4_t tmp;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            tmp[col * 4 + row] =
                b[col * 4 + 0] * a[0 * 4 + row] +
                b[col * 4 + 1] * a[1 * 4 + row] +
                b[col * 4 + 2] * a[2 * 4 + row] +
                b[col * 4 + 3] * a[3 * 4 + row];
        }
    }
    memcpy(out, tmp, sizeof(tmp));
}

static void VK_Shadow_Ortho(float xmin, float xmax, float ymin, float ymax,
                            float znear, float zfar, mat4_t matrix)
{
    float width = xmax - xmin;
    float height = ymax - ymin;
    float depth = zfar - znear;

    matrix[0] = 2.0f / width;
    matrix[4] = 0.0f;
    matrix[8] = 0.0f;
    matrix[12] = -(xmax + xmin) / width;

    matrix[1] = 0.0f;
    matrix[5] = 2.0f / height;
    matrix[9] = 0.0f;
    matrix[13] = -(ymax + ymin) / height;

    matrix[2] = 0.0f;
    matrix[6] = 0.0f;
    matrix[10] = -2.0f / depth;
    matrix[14] = -(zfar + znear) / depth;

    matrix[3] = 0.0f;
    matrix[7] = 0.0f;
    matrix[11] = 0.0f;
    matrix[15] = 1.0f;
}

static void VK_Shadow_BuildPush(const shadow_view_desc_t *view,
                                renderer_view_push_t *push)
{
    memset(push, 0, sizeof(*push));
    Matrix_FromOriginAxis(view->origin, view->axis, push->view);

    if (view->view_type == SHADOW_VIEW_SUN_CASCADE) {
        float half = max(view->ortho_size * 0.5f, 64.0f);
        float near_z = max(view->near_z, 0.0f);
        float far_z = max(view->far_z, near_z + 1.0f);
        VK_Shadow_Ortho(-half, half, -half, half, -far_z, -near_z,
                        push->proj);
    } else {
        float near_z = max(view->near_z, 1.0f);
        float far_z = max(view->far_z, near_z + 1.0f);
        Matrix_Frustum(view->fov_x, view->fov_y, 1.0f, near_z, far_z,
                       push->proj);
    }
}

static void VK_Shadow_DestroyVertexBuffer(vk_shadow_frame_resources_t *frame)
{
    if (!frame || !vk_shadow.ctx || !vk_shadow.ctx->device) {
        return;
    }
    VkDevice device = vk_shadow.ctx->device;
    if (frame->vertex_staging_mapped) {
        vkUnmapMemory(device, frame->vertex_staging_memory);
        frame->vertex_staging_mapped = NULL;
    }
    if (frame->vertex_staging_buffer) {
        vkDestroyBuffer(device, frame->vertex_staging_buffer, NULL);
        frame->vertex_staging_buffer = VK_NULL_HANDLE;
    }
    if (frame->vertex_staging_memory) {
        vkFreeMemory(device, frame->vertex_staging_memory, NULL);
        frame->vertex_staging_memory = VK_NULL_HANDLE;
    }
    if (frame->vertex_buffer) {
        vkDestroyBuffer(device, frame->vertex_buffer, NULL);
        frame->vertex_buffer = VK_NULL_HANDLE;
    }
    if (frame->vertex_memory) {
        vkFreeMemory(device, frame->vertex_memory, NULL);
        frame->vertex_memory = VK_NULL_HANDLE;
    }
    frame->vertex_buffer_bytes = 0;
    frame->vertex_upload_bytes = 0;
    frame->vertex_upload_recorded = false;
}

static bool VK_Shadow_GrowStreamBuffer(size_t current, size_t needed,
                                       size_t *out_capacity)
{
    if (!out_capacity || !needed) {
        Com_SetLastError("Vulkan shadow: invalid stream buffer capacity request");
        return false;
    }

    size_t capacity = current ? current : VK_SHADOW_STREAM_BUFFER_MIN_BYTES;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }

    *out_capacity = capacity;
    return true;
}

static void VK_Shadow_DestroyUniformBuffer(vk_shadow_frame_resources_t *frame)
{
    if (!frame || !vk_shadow.ctx || !vk_shadow.ctx->device) {
        return;
    }
    VkDevice device = vk_shadow.ctx->device;
    if (frame->uniform_mapped) {
        vkUnmapMemory(device, frame->uniform_memory);
        frame->uniform_mapped = NULL;
    }
    if (frame->uniform_buffer) {
        vkDestroyBuffer(device, frame->uniform_buffer, NULL);
        frame->uniform_buffer = VK_NULL_HANDLE;
    }
    if (frame->uniform_memory) {
        vkFreeMemory(device, frame->uniform_memory, NULL);
        frame->uniform_memory = VK_NULL_HANDLE;
    }
}

static bool VK_Shadow_CreateUniformBuffer(vk_shadow_frame_resources_t *frame)
{
    if (!frame) {
        return false;
    }
    VkDevice device = vk_shadow.ctx->device;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(vk_shadow.uniform),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_Shadow_Check(vkCreateBuffer(device, &buffer_info, NULL,
                                        &frame->uniform_buffer),
                         "vkCreateBuffer(shadow uniforms)")) {
        return false;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, frame->uniform_buffer, &req);
    uint32_t memory_index =
        VK_Shadow_FindMemoryType(req.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_index == UINT32_MAX) {
        Com_EPrintf("Vulkan shadow: suitable uniform memory type not found\n");
        VK_Shadow_DestroyUniformBuffer(frame);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_index,
    };
    if (!VK_Shadow_Check(vkAllocateMemory(device, &alloc_info, NULL,
                                          &frame->uniform_memory),
                         "vkAllocateMemory(shadow uniforms)")) {
        VK_Shadow_DestroyUniformBuffer(frame);
        return false;
    }
    if (!VK_Shadow_Check(vkBindBufferMemory(device, frame->uniform_buffer,
                                            frame->uniform_memory, 0),
                         "vkBindBufferMemory(shadow uniforms)")) {
        VK_Shadow_DestroyUniformBuffer(frame);
        return false;
    }
    if (!VK_Shadow_Check(vkMapMemory(device, frame->uniform_memory, 0,
                                     req.size, 0, &frame->uniform_mapped),
                         "vkMapMemory(shadow uniforms)")) {
        VK_Shadow_DestroyUniformBuffer(frame);
        return false;
    }

    memset(&vk_shadow.uniform, 0, sizeof(vk_shadow.uniform));
    vk_shadow.uniform.sun[0] = -1.0f;
    return true;
}

static void VK_Shadow_DestroyDescriptors(void)
{
    if (!vk_shadow.ctx || !vk_shadow.ctx->device) {
        return;
    }
    VkDevice device = vk_shadow.ctx->device;
    if (vk_shadow.descriptor_pool) {
        vkDestroyDescriptorPool(device, vk_shadow.descriptor_pool, NULL);
        vk_shadow.descriptor_pool = VK_NULL_HANDLE;
        for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
            vk_shadow.frame_resources[i].descriptor_set = VK_NULL_HANDLE;
        }
    }
    if (vk_shadow.descriptor_set_layout) {
        vkDestroyDescriptorSetLayout(device, vk_shadow.descriptor_set_layout,
                                     NULL);
        vk_shadow.descriptor_set_layout = VK_NULL_HANDLE;
    }
}

static bool VK_Shadow_CreateDescriptors(void)
{
    VkDevice device = vk_shadow.ctx->device;
    VkDescriptorSetLayoutBinding bindings[4] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 3,
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
    if (!VK_Shadow_Check(vkCreateDescriptorSetLayout(device, &layout_info,
                                                     NULL,
                                                     &vk_shadow.descriptor_set_layout),
                         "vkCreateDescriptorSetLayout(shadow)")) {
        return false;
    }

    VkDescriptorPoolSize pool_sizes[2] = {
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 3 * VK_MAX_FRAMES_IN_FLIGHT,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = VK_MAX_FRAMES_IN_FLIGHT,
        },
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = VK_MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = q_countof(pool_sizes),
        .pPoolSizes = pool_sizes,
    };
    if (!VK_Shadow_Check(vkCreateDescriptorPool(device, &pool_info, NULL,
                                                &vk_shadow.descriptor_pool),
                         "vkCreateDescriptorPool(shadow)")) {
        VK_Shadow_DestroyDescriptors();
        return false;
    }

    VkDescriptorSetLayout set_layouts[VK_MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        set_layouts[i] = vk_shadow.descriptor_set_layout;
    }
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_shadow.descriptor_pool,
        .descriptorSetCount = VK_MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts = set_layouts,
    };
    VkDescriptorSet descriptor_sets[VK_MAX_FRAMES_IN_FLIGHT];
    if (!VK_Shadow_Check(vkAllocateDescriptorSets(device, &alloc_info,
                                                  descriptor_sets),
                         "vkAllocateDescriptorSets(shadow)")) {
        VK_Shadow_DestroyDescriptors();
        return false;
    }
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_shadow.frame_resources[i].descriptor_set = descriptor_sets[i];
    }
    return true;
}

static void VK_Shadow_UpdateDescriptorSet(void)
{
    if (!vk_shadow.sampler || !vk_shadow.compare_sampler ||
        !vk_shadow.array_view) {
        return;
    }

    VkDescriptorImageInfo image_info = {
        .sampler = vk_shadow.sampler,
        .imageView = vk_shadow.array_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo compare_info = {
        .sampler = vk_shadow.compare_sampler,
        .imageView = vk_shadow.array_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo moment_info = {
        .sampler = vk_shadow.moment_sampler ? vk_shadow.moment_sampler
                                            : vk_shadow.sampler,
        .imageView = vk_shadow.moment_array_view ? vk_shadow.moment_array_view
                                                 : vk_shadow.array_view,
        .imageLayout = vk_shadow.moment_array_view
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    };
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_shadow_frame_resources_t *frame = &vk_shadow.frame_resources[i];
        if (!frame->descriptor_set || !frame->uniform_buffer) {
            continue;
        }
        VkDescriptorBufferInfo buffer_info = {
            .buffer = frame->uniform_buffer,
            .offset = 0,
            .range = sizeof(vk_shadow.uniform),
        };
        VkWriteDescriptorSet writes[4] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frame->descriptor_set,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frame->descriptor_set,
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frame->descriptor_set,
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &moment_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frame->descriptor_set,
                .dstBinding = 3,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &compare_info,
            },
        };
        vkUpdateDescriptorSets(vk_shadow.ctx->device, q_countof(writes), writes,
                               0, NULL);
    }
}

static bool VK_Shadow_EnsureVertexBuffer(size_t bytes)
{
    vk_shadow_frame_resources_t *frame = VK_Shadow_CurrentFrameResources();
    if (bytes == 0) {
        return true;
    }
    if (!frame || !vk_shadow.ctx || !vk_shadow.ctx->device) {
        return false;
    }
    if (frame->vertex_buffer && frame->vertex_memory &&
        frame->vertex_staging_buffer && frame->vertex_staging_memory &&
        frame->vertex_staging_mapped && frame->vertex_buffer_bytes >= bytes) {
        return true;
    }

    size_t capacity;
    if (!VK_Shadow_GrowStreamBuffer(frame->vertex_buffer_bytes, bytes,
                                    &capacity)) {
        return false;
    }

    VK_Shadow_DestroyVertexBuffer(frame);

    VkDevice device = vk_shadow.ctx->device;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_Shadow_Check(vkCreateBuffer(device, &buffer_info, NULL,
                                        &frame->vertex_buffer),
                         "vkCreateBuffer(shadow vertices)")) {
        return false;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, frame->vertex_buffer, &req);
    uint32_t memory_index =
        VK_Shadow_FindMemoryType(req.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_index == UINT32_MAX) {
        Com_EPrintf("Vulkan shadow: suitable device-local vertex memory type not found\n");
        VK_Shadow_DestroyVertexBuffer(frame);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_index,
    };
    if (!VK_Shadow_Check(vkAllocateMemory(device, &alloc_info, NULL,
                                          &frame->vertex_memory),
                         "vkAllocateMemory(shadow vertices)")) {
        VK_Shadow_DestroyVertexBuffer(frame);
        return false;
    }
    if (!VK_Shadow_Check(vkBindBufferMemory(device, frame->vertex_buffer,
                                            frame->vertex_memory, 0),
                         "vkBindBufferMemory(shadow vertices)")) {
        VK_Shadow_DestroyVertexBuffer(frame);
        return false;
    }
    VkBufferCreateInfo staging_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_Shadow_Check(vkCreateBuffer(device, &staging_info, NULL,
                                        &frame->vertex_staging_buffer),
                         "vkCreateBuffer(shadow vertex staging)")) {
        VK_Shadow_DestroyVertexBuffer(frame);
        return false;
    }

    VkMemoryRequirements staging_req;
    vkGetBufferMemoryRequirements(device, frame->vertex_staging_buffer,
                                  &staging_req);
    uint32_t staging_memory_index = VK_Shadow_FindMemoryType(
        staging_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (staging_memory_index == UINT32_MAX) {
        Com_EPrintf("Vulkan shadow: suitable host-visible staging memory type not found\n");
        VK_Shadow_DestroyVertexBuffer(frame);
        return false;
    }
    VkMemoryAllocateInfo staging_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = staging_req.size,
        .memoryTypeIndex = staging_memory_index,
    };
    if (!VK_Shadow_Check(vkAllocateMemory(device, &staging_alloc, NULL,
                                          &frame->vertex_staging_memory),
                         "vkAllocateMemory(shadow vertex staging)") ||
        !VK_Shadow_Check(vkBindBufferMemory(device, frame->vertex_staging_buffer,
                                            frame->vertex_staging_memory, 0),
                         "vkBindBufferMemory(shadow vertex staging)") ||
        !VK_Shadow_Check(vkMapMemory(device, frame->vertex_staging_memory, 0,
                                     capacity, 0, &frame->vertex_staging_mapped),
                         "vkMapMemory(shadow vertex staging)")) {
        VK_Shadow_DestroyVertexBuffer(frame);
        return false;
    }

    frame->vertex_buffer_bytes = capacity;
    return true;
}

static void VK_Shadow_DestroyResources(void)
{
    if (!vk_shadow.ctx || !vk_shadow.ctx->device) {
        return;
    }
    VkDevice device = vk_shadow.ctx->device;

    for (int i = 0; i < VK_SHADOW_MAX_PAGES; i++) {
        if (vk_shadow.moment_framebuffers[i]) {
            vkDestroyFramebuffer(device, vk_shadow.moment_framebuffers[i],
                                 NULL);
            vk_shadow.moment_framebuffers[i] = VK_NULL_HANDLE;
        }
        if (vk_shadow.framebuffers[i]) {
            vkDestroyFramebuffer(device, vk_shadow.framebuffers[i], NULL);
            vk_shadow.framebuffers[i] = VK_NULL_HANDLE;
        }
        if (vk_shadow.moment_layer_views[i]) {
            vkDestroyImageView(device, vk_shadow.moment_layer_views[i],
                               NULL);
            vk_shadow.moment_layer_views[i] = VK_NULL_HANDLE;
        }
        if (vk_shadow.layer_views[i]) {
            vkDestroyImageView(device, vk_shadow.layer_views[i], NULL);
            vk_shadow.layer_views[i] = VK_NULL_HANDLE;
        }
    }
    if (vk_shadow.moment_pipeline) {
        vkDestroyPipeline(device, vk_shadow.moment_pipeline, NULL);
        vk_shadow.moment_pipeline = VK_NULL_HANDLE;
    }
    if (vk_shadow.pipeline) {
        vkDestroyPipeline(device, vk_shadow.pipeline, NULL);
        vk_shadow.pipeline = VK_NULL_HANDLE;
    }
    if (vk_shadow.pipeline_layout) {
        vkDestroyPipelineLayout(device, vk_shadow.pipeline_layout, NULL);
        vk_shadow.pipeline_layout = VK_NULL_HANDLE;
    }
    if (vk_shadow.render_pass) {
        vkDestroyRenderPass(device, vk_shadow.render_pass, NULL);
        vk_shadow.render_pass = VK_NULL_HANDLE;
    }
    if (vk_shadow.sampler) {
        vkDestroySampler(device, vk_shadow.sampler, NULL);
        vk_shadow.sampler = VK_NULL_HANDLE;
    }
    if (vk_shadow.compare_sampler) {
        vkDestroySampler(device, vk_shadow.compare_sampler, NULL);
        vk_shadow.compare_sampler = VK_NULL_HANDLE;
    }
    if (vk_shadow.moment_sampler) {
        vkDestroySampler(device, vk_shadow.moment_sampler, NULL);
        vk_shadow.moment_sampler = VK_NULL_HANDLE;
    }
    if (vk_shadow.moment_array_view) {
        vkDestroyImageView(device, vk_shadow.moment_array_view, NULL);
        vk_shadow.moment_array_view = VK_NULL_HANDLE;
    }
    if (vk_shadow.array_view) {
        vkDestroyImageView(device, vk_shadow.array_view, NULL);
        vk_shadow.array_view = VK_NULL_HANDLE;
    }
    if (vk_shadow.moment_image) {
        vkDestroyImage(device, vk_shadow.moment_image, NULL);
        vk_shadow.moment_image = VK_NULL_HANDLE;
    }
    if (vk_shadow.image) {
        vkDestroyImage(device, vk_shadow.image, NULL);
        vk_shadow.image = VK_NULL_HANDLE;
    }
    if (vk_shadow.moment_memory) {
        vkFreeMemory(device, vk_shadow.moment_memory, NULL);
        vk_shadow.moment_memory = VK_NULL_HANDLE;
    }
    if (vk_shadow.memory) {
        vkFreeMemory(device, vk_shadow.memory, NULL);
        vk_shadow.memory = VK_NULL_HANDLE;
    }

    vk_shadow.resources_ok = false;
    vk_shadow.layout_initialized = false;
    vk_shadow.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_shadow.moment_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_shadow.resolution = 0;
    vk_shadow.mip_levels = 1;
}

static bool VK_Shadow_CreateRenderPass(VkDevice device)
{
    VkAttachmentDescription attachments[2] = {
        {
            .format = vk_shadow.moment_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk_shadow.depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };
    VkAttachmentDescription depth = {
        .format = vk_shadow.depth_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depth_ref = {
        .attachment = vk_shadow.storage_family == SHADOW_STORAGE_MOMENT ? 1 : 0,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount =
            vk_shadow.storage_family == SHADOW_STORAGE_MOMENT ? 1u : 0u,
        .pColorAttachments =
            vk_shadow.storage_family == SHADOW_STORAGE_MOMENT ? &color_ref : NULL,
        .pDepthStencilAttachment = &depth_ref,
    };
    VkRenderPassCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount =
            vk_shadow.storage_family == SHADOW_STORAGE_MOMENT ? 2u : 1u,
        .pAttachments = vk_shadow.storage_family == SHADOW_STORAGE_MOMENT
            ? attachments
            : &depth,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    return VK_Shadow_Check(vkCreateRenderPass(device, &info, NULL,
                                              &vk_shadow.render_pass),
                           "vkCreateRenderPass(shadow)");
}

static bool VK_Shadow_CreatePipeline(VkDevice device)
{
    VkPushConstantRange push = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(vk_shadow_push_t),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    if (!VK_Shadow_Check(vkCreatePipelineLayout(device, &layout_info, NULL,
                                                &vk_shadow.pipeline_layout),
                         "vkCreatePipelineLayout(shadow)")) {
        return false;
    }

    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vert_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_world_vert_spv_size,
        .pCode = vk_world_vert_spv,
    };
    if (!VK_Shadow_Check(vkCreateShaderModule(device, &vert_info, NULL, &vert),
                         "vkCreateShaderModule(shadow vert)")) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
    };
    uint32_t stage_count = 1;
    VkShaderModule frag = VK_NULL_HANDLE;
    if (vk_shadow.storage_family == SHADOW_STORAGE_MOMENT) {
        VkShaderModuleCreateInfo frag_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = vk_shadow_moment_frag_spv_size,
            .pCode = vk_shadow_moment_frag_spv,
        };
        if (!VK_Shadow_Check(vkCreateShaderModule(device, &frag_info, NULL,
                                                  &frag),
                             "vkCreateShaderModule(shadow moment frag)")) {
            vkDestroyShaderModule(device, vert, NULL);
            return false;
        }
        stages[1] = (VkPipelineShaderStageCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        };
        stage_count = 2;
    }
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(vk_shadow_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[6] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_shadow_vertex_t, pos) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_shadow_vertex_t, uv) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_shadow_vertex_t, lm_uv) },
        { .location = 3, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(vk_shadow_vertex_t, color) },
        { .location = 4, .binding = 0, .format = VK_FORMAT_R8_UINT, .offset = offsetof(vk_shadow_vertex_t, flags) },
        { .location = 5, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_shadow_vertex_t, normal) },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = q_countof(attrs),
        .pVertexAttributeDescriptions = attrs,
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
        .depthBiasEnable = VK_TRUE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount =
            vk_shadow.storage_family == SHADOW_STORAGE_MOMENT ? 1u : 0u,
        .pAttachments =
            vk_shadow.storage_family == SHADOW_STORAGE_MOMENT
                ? &blend_attachment
                : NULL,
    };
    VkDynamicState states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = q_countof(states),
        .pDynamicStates = states,
    };
    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = stage_count,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk_shadow.pipeline_layout,
        .renderPass = vk_shadow.render_pass,
        .subpass = 0,
    };
    bool ok = VK_Shadow_Check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE,
                                                        1, &info, NULL,
                                                        &vk_shadow.pipeline),
                              "vkCreateGraphicsPipelines(shadow)");
    vkDestroyShaderModule(device, vert, NULL);
    if (frag) {
        vkDestroyShaderModule(device, frag, NULL);
    }
    return ok;
}

static bool VK_Shadow_EnsureResources(int requested_resolution,
                                      shadow_storage_family_t storage,
                                      bool *allocated)
{
    if (allocated) {
        *allocated = false;
    }
    if (!vk_shadow.initialized || !vk_shadow.ctx || !vk_shadow.ctx->device) {
        return false;
    }
    if (!VK_Shadow_ValidStorageFamily(storage)) {
        return false;
    }

    int resolution = VK_Shadow_ClampResolution(requested_resolution);
    VkFormat depth_format = vk_shadow.depth_format;
    if (depth_format == VK_FORMAT_UNDEFINED) {
        return false;
    }
    VkFormat moment_format = vk_shadow.moment_format;
    if (storage == SHADOW_STORAGE_MOMENT &&
        moment_format == VK_FORMAT_UNDEFINED) {
        return false;
    }
    if (vk_shadow.resources_ok && vk_shadow.resolution >= resolution &&
        vk_shadow.depth_format == depth_format &&
        vk_shadow.storage_family == storage) {
        return true;
    }

    VkDevice device = vk_shadow.ctx->device;
    if (vk_shadow.resources_ok) {
        vkDeviceWaitIdle(device);
    }
    VK_Shadow_DestroyResources();
    vk_shadow.depth_format = depth_format;
    vk_shadow.moment_format = moment_format;
    vk_shadow.storage_family = storage;
    vk_shadow.mip_levels = storage == SHADOW_STORAGE_MOMENT
        ? VK_Shadow_MipLevels(resolution)
        : 1;

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_format,
        .extent = { (uint32_t)resolution, (uint32_t)resolution, 1 },
        .mipLevels = 1,
        .arrayLayers = VK_SHADOW_MAX_PAGES,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!VK_Shadow_Check(vkCreateImage(device, &image_info, NULL,
                                       &vk_shadow.image),
                         "vkCreateImage(shadow depth array)")) {
        return false;
    }

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, vk_shadow.image, &req);
    uint32_t memory_index =
        VK_Shadow_FindMemoryType(req.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_index == UINT32_MAX) {
        Com_EPrintf("Vulkan shadow: suitable image memory type not found\n");
        VK_Shadow_DestroyResources();
        return false;
    }
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_index,
    };
    if (!VK_Shadow_Check(vkAllocateMemory(device, &alloc, NULL,
                                          &vk_shadow.memory),
                         "vkAllocateMemory(shadow depth array)")) {
        VK_Shadow_DestroyResources();
        return false;
    }
    if (!VK_Shadow_Check(vkBindImageMemory(device, vk_shadow.image,
                                           vk_shadow.memory, 0),
                         "vkBindImageMemory(shadow depth array)")) {
        VK_Shadow_DestroyResources();
        return false;
    }

    VkImageViewCreateInfo array_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk_shadow.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = depth_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = VK_SHADOW_MAX_PAGES,
        },
    };
    if (!VK_Shadow_Check(vkCreateImageView(device, &array_view_info, NULL,
                                           &vk_shadow.array_view),
                         "vkCreateImageView(shadow array)")) {
        VK_Shadow_DestroyResources();
        return false;
    }

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };
    if (!VK_Shadow_Check(vkCreateSampler(device, &sampler_info, NULL,
                                         &vk_shadow.sampler),
                         "vkCreateSampler(shadow depth sampler)")) {
        VK_Shadow_DestroyResources();
        return false;
    }

    VkSamplerCreateInfo compare_sampler_info = sampler_info;
    compare_sampler_info.compareEnable = VK_TRUE;
    compare_sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (!VK_Shadow_Check(vkCreateSampler(device, &compare_sampler_info, NULL,
                                         &vk_shadow.compare_sampler),
                         "vkCreateSampler(shadow compare sampler)")) {
        VK_Shadow_DestroyResources();
        return false;
    }

    if (storage == SHADOW_STORAGE_MOMENT) {
        VkImageCreateInfo moment_image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = moment_format,
            .extent = { (uint32_t)resolution, (uint32_t)resolution, 1 },
            .mipLevels = (uint32_t)vk_shadow.mip_levels,
            .arrayLayers = VK_SHADOW_MAX_PAGES,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        if (!VK_Shadow_Check(vkCreateImage(device, &moment_image_info, NULL,
                                           &vk_shadow.moment_image),
                             "vkCreateImage(shadow moment array)")) {
            VK_Shadow_DestroyResources();
            return false;
        }

        VkMemoryRequirements moment_req;
        vkGetImageMemoryRequirements(device, vk_shadow.moment_image,
                                     &moment_req);
        uint32_t moment_memory_index =
            VK_Shadow_FindMemoryType(moment_req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (moment_memory_index == UINT32_MAX) {
            Com_EPrintf("Vulkan shadow: suitable moment image memory type not found\n");
            VK_Shadow_DestroyResources();
            return false;
        }
        VkMemoryAllocateInfo moment_alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = moment_req.size,
            .memoryTypeIndex = moment_memory_index,
        };
        if (!VK_Shadow_Check(vkAllocateMemory(device, &moment_alloc, NULL,
                                              &vk_shadow.moment_memory),
                             "vkAllocateMemory(shadow moment array)")) {
            VK_Shadow_DestroyResources();
            return false;
        }
        if (!VK_Shadow_Check(vkBindImageMemory(device, vk_shadow.moment_image,
                                               vk_shadow.moment_memory, 0),
                             "vkBindImageMemory(shadow moment array)")) {
            VK_Shadow_DestroyResources();
            return false;
        }

        VkImageViewCreateInfo moment_array_view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk_shadow.moment_image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = moment_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = (uint32_t)vk_shadow.mip_levels,
                .baseArrayLayer = 0,
                .layerCount = VK_SHADOW_MAX_PAGES,
            },
        };
        if (!VK_Shadow_Check(vkCreateImageView(device,
                                               &moment_array_view_info,
                                               NULL,
                                               &vk_shadow.moment_array_view),
                             "vkCreateImageView(shadow moment array)")) {
            VK_Shadow_DestroyResources();
            return false;
        }

        VkSamplerCreateInfo moment_sampler_info = sampler_info;
        moment_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        moment_sampler_info.maxLod = (float)(vk_shadow.mip_levels - 1);
        if (!VK_Shadow_Check(vkCreateSampler(device, &moment_sampler_info,
                                             NULL,
                                             &vk_shadow.moment_sampler),
                             "vkCreateSampler(shadow moment sampler)")) {
            VK_Shadow_DestroyResources();
            return false;
        }
    }

    if (!VK_Shadow_CreateRenderPass(device) ||
        !VK_Shadow_CreatePipeline(device)) {
        VK_Shadow_DestroyResources();
        return false;
    }

    for (int i = 0; i < VK_SHADOW_MAX_PAGES; i++) {
        VkImageViewCreateInfo layer_info = array_view_info;
        layer_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layer_info.subresourceRange.baseArrayLayer = (uint32_t)i;
        layer_info.subresourceRange.layerCount = 1;
        if (!VK_Shadow_Check(vkCreateImageView(device, &layer_info, NULL,
                                               &vk_shadow.layer_views[i]),
                             "vkCreateImageView(shadow layer)")) {
            VK_Shadow_DestroyResources();
            return false;
        }
        VkImageView attachments[2] = { vk_shadow.layer_views[i], VK_NULL_HANDLE };
        uint32_t attachment_count = 1;
        if (storage == SHADOW_STORAGE_MOMENT) {
            VkImageViewCreateInfo moment_layer_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = vk_shadow.moment_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = moment_format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = (uint32_t)i,
                    .layerCount = 1,
                },
            };
            if (!VK_Shadow_Check(vkCreateImageView(device, &moment_layer_info,
                                                   NULL,
                                                   &vk_shadow.moment_layer_views[i]),
                                 "vkCreateImageView(shadow moment layer)")) {
                VK_Shadow_DestroyResources();
                return false;
            }
            attachments[0] = vk_shadow.moment_layer_views[i];
            attachments[1] = vk_shadow.layer_views[i];
            attachment_count = 2;
        }
        VkFramebufferCreateInfo fb = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk_shadow.render_pass,
            .attachmentCount = attachment_count,
            .pAttachments = attachments,
            .width = (uint32_t)resolution,
            .height = (uint32_t)resolution,
            .layers = 1,
        };
        if (!VK_Shadow_Check(vkCreateFramebuffer(device, &fb, NULL,
                                                 &vk_shadow.framebuffers[i]),
                             "vkCreateFramebuffer(shadow layer)")) {
            VK_Shadow_DestroyResources();
            return false;
        }
    }

    vk_shadow.resolution = resolution;
    vk_shadow.resources_ok = true;
    vk_shadow.reallocated_this_frame = true;
    vk_shadow.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_shadow.layout_initialized = false;
    VK_Shadow_UpdateDescriptorSet();
    if (allocated) {
        *allocated = true;
    }
    return true;
}

static bool VK_Shadow_EnsureCpuCapacity(uint32_t needed)
{
    if (needed <= vk_shadow.vertex_capacity) {
        return true;
    }

    uint32_t new_capacity;
    if (!VK_Shadow_GrowCapacity(vk_shadow.vertex_capacity, needed,
                                &new_capacity, "vertex")) {
        return false;
    }

    size_t vertex_bytes;
    if (!VK_Shadow_ArrayBytes((size_t)new_capacity, sizeof(*vk_shadow.vertices),
                              &vertex_bytes, "vertex")) {
        return false;
    }

    vk_shadow_vertex_t *new_vertices =
        realloc(vk_shadow.vertices, vertex_bytes);
    if (!new_vertices) {
        Com_EPrintf("Vulkan shadow: out of memory for shadow vertices\n");
        return false;
    }
    vk_shadow.vertices = new_vertices;
    vk_shadow.vertex_capacity = new_capacity;
    return true;
}

static bool VK_Shadow_AddTriangle(const vec3_t a, const vec3_t b,
                                  const vec3_t c)
{
    uint32_t needed_vertices;
    if (!VK_Shadow_AddCount(vk_shadow.vertex_count, 3, &needed_vertices,
                            "triangle vertex") ||
        !VK_Shadow_EnsureCpuCapacity(needed_vertices)) {
        return false;
    }

    size_t triangle_bytes;
    if (!VK_Shadow_ArrayBytes(3, sizeof(*vk_shadow.vertices),
                              &triangle_bytes, "triangle vertex")) {
        return false;
    }

    vk_shadow_vertex_t *v = &vk_shadow.vertices[vk_shadow.vertex_count];
    memset(v, 0, triangle_bytes);
    VectorCopy(a, v[0].pos);
    VectorCopy(b, v[1].pos);
    VectorCopy(c, v[2].pos);
    vec3_t ab, ac, normal;
    VectorSubtract(b, a, ab);
    VectorSubtract(c, a, ac);
    CrossProduct(ab, ac, normal);
    if (VectorNormalize(normal) <= 0.0f) {
        VectorSet(normal, 0.0f, 0.0f, 1.0f);
    }
    VectorCopy(normal, v[0].normal);
    VectorCopy(normal, v[1].normal);
    VectorCopy(normal, v[2].normal);
    v[0].color = v[1].color = v[2].color = 0xffffffffu;
    v[0].base_alpha = v[1].base_alpha = v[2].base_alpha = 255;
    vk_shadow.vertex_count = needed_vertices;
    return true;
}

static inline const mvertex_t *VK_Shadow_SurfEdgeVertex(const bsp_t *bsp,
                                                        const msurfedge_t *edge)
{
    if (!bsp || !edge || edge->edge >= (uint32_t)bsp->numedges) {
        return NULL;
    }
    const medge_t *src_edge = &bsp->edges[edge->edge];
    uint32_t vertex = src_edge->v[edge->vert ? 1 : 0];
    if (vertex >= (uint32_t)bsp->numvertices) {
        return NULL;
    }
    return &bsp->vertices[vertex];
}

static void VK_Shadow_AddPointToBounds(const vec3_t point,
                                       vec3_t mins,
                                       vec3_t maxs)
{
    for (int i = 0; i < 3; i++) {
        if (point[i] < mins[i]) {
            mins[i] = point[i];
        }
        if (point[i] > maxs[i]) {
            maxs[i] = point[i];
        }
    }
}

static bool VK_Shadow_ViewTouchesBounds(const shadow_view_desc_t *view,
                                        const vec3_t mins,
                                        const vec3_t maxs)
{
    vec3_t center, extents;
    VectorAvg(mins, maxs, center);
    VectorSubtract(maxs, mins, extents);
    float radius = VectorLength(extents) * 0.5f;

    if (view->view_type == SHADOW_VIEW_SUN_CASCADE) {
        float half = max(view->ortho_size * 0.5f, 64.0f);
        vec3_t delta;
        VectorSubtract(center, view->origin, delta);
        float side = fabsf(DotProduct(delta, view->axis[1]));
        float up = fabsf(DotProduct(delta, view->axis[2]));
        float forward = -DotProduct(delta, view->axis[0]);
        return side <= half + radius && up <= half + radius &&
               forward <= view->far_z + radius;
    }

    vec3_t delta;
    VectorSubtract(center, view->origin, delta);
    float dist = VectorLength(delta);
    if (dist - radius > view->far_z || dist + radius < view->near_z) {
        return false;
    }

    float forward = DotProduct(delta, view->axis[0]);
    if (forward + radius < view->near_z || forward - radius > view->far_z) {
        return false;
    }
    float side = fabsf(DotProduct(delta, view->axis[1]));
    float up = fabsf(DotProduct(delta, view->axis[2]));
    float tan_x = tanf(DEG2RAD(max(view->fov_x, 1.0f)) * 0.5f);
    float tan_y = tanf(DEG2RAD(max(view->fov_y, 1.0f)) * 0.5f);
    return side <= forward * tan_x + radius &&
           up <= forward * tan_y + radius;
}

static const vk_shadow_face_bounds_t *VK_Shadow_WorldFaceBounds(const bsp_t *bsp)
{
    if (vk_shadow_world_cache.bounds && vk_shadow_world_cache.bsp == bsp &&
        vk_shadow_world_cache.checksum == bsp->checksum &&
        vk_shadow_world_cache.numfaces == bsp->numfaces) {
        return vk_shadow_world_cache.bounds;
    }

    VK_Shadow_FreeWorldCache();
    if (!bsp || !bsp->faces || bsp->numfaces <= 0) {
        return NULL;
    }

    size_t bounds_bytes;
    if (!VK_Shadow_ArrayBytes((size_t)bsp->numfaces, sizeof(vk_shadow_face_bounds_t),
                              &bounds_bytes, "world face bounds")) {
        return NULL;
    }

    vk_shadow_face_bounds_t *bounds = malloc(bounds_bytes);
    if (!bounds) {
        Com_EPrintf("Vulkan shadow: out of memory for world face bounds\n");
        return NULL;
    }
    for (int i = 0; i < bsp->numfaces; i++) {
        const mface_t *face = &bsp->faces[i];
        ClearBounds(bounds[i].mins, bounds[i].maxs);
        if (!face->firstsurfedge) {
            continue;
        }
        for (int j = 0; j < face->numsurfedges; j++) {
            const mvertex_t *v = VK_Shadow_SurfEdgeVertex(bsp,
                                                          &face->firstsurfedge[j]);
            if (v) {
                VK_Shadow_AddPointToBounds(v->point, bounds[i].mins,
                                           bounds[i].maxs);
            }
        }
    }

    vk_shadow_world_cache.bsp = bsp;
    vk_shadow_world_cache.checksum = bsp->checksum;
    vk_shadow_world_cache.numfaces = bsp->numfaces;
    vk_shadow_world_cache.bounds = bounds;
    return bounds;
}

static bool VK_Shadow_AddWorldDepth(const shadow_view_desc_t *view)
{
    const bsp_t *bsp = VK_World_GetBsp();
    if (!bsp || !bsp->faces || !bsp->edges || !bsp->vertices) {
        return true;
    }

    const vk_shadow_face_bounds_t *bounds = VK_Shadow_WorldFaceBounds(bsp);
    if (!bounds) {
        return true;
    }

    for (int i = 0; i < bsp->numfaces; i++) {
        const mface_t *face = &bsp->faces[i];
        if (!face->firstsurfedge || face->numsurfedges < 3) {
            continue;
        }
        int flags = face->drawflags;
        if (flags & (SURF_SKY | SURF_NODRAW | SURF_TRANS_MASK)) {
            continue;
        }
        const mvertex_t *v0 = VK_Shadow_SurfEdgeVertex(bsp,
                                                       &face->firstsurfedge[0]);
        if (!v0) {
            continue;
        }
        vk_shadow.world_faces_considered++;
        if (view && !VK_Shadow_ViewTouchesBounds(view, bounds[i].mins,
                                                 bounds[i].maxs)) {
            continue;
        }
        vk_shadow.world_faces_submitted++;
        for (int j = 1; j < face->numsurfedges - 1; j++) {
            const mvertex_t *v1 = VK_Shadow_SurfEdgeVertex(bsp,
                                                           &face->firstsurfedge[j]);
            const mvertex_t *v2 = VK_Shadow_SurfEdgeVertex(bsp,
                                                           &face->firstsurfedge[j + 1]);
            if (!v1 || !v2) {
                continue;
            }
            if (!VK_Shadow_AddTriangle(v0->point, v1->point, v2->point)) {
                return false;
            }
        }
    }
    return true;
}

static bool VK_Shadow_EmitCasterTriangle(const vec3_t a,
                                         const vec3_t b,
                                         const vec3_t c,
                                         void *userdata)
{
    (void)userdata;
    return VK_Shadow_AddTriangle(a, b, c);
}

static bool VK_Shadow_EmitBoundsCaster(const shadow_caster_t *caster)
{
    if (!caster || !(caster->flags & RF_CASTSHADOW)) {
        return false;
    }

    const vec_t *mins = caster->bounds[0];
    const vec_t *maxs = caster->bounds[1];
    if (maxs[0] <= mins[0] || maxs[1] <= mins[1] || maxs[2] <= mins[2]) {
        return false;
    }

    vec3_t corners[8];
    for (int i = 0; i < 8; i++) {
        VectorSet(corners[i],
                  (i & 1) ? maxs[0] : mins[0],
                  (i & 2) ? maxs[1] : mins[1],
                  (i & 4) ? maxs[2] : mins[2]);
    }

    static const int tris[12][3] = {
        {0, 2, 3}, {0, 3, 1},
        {4, 5, 7}, {4, 7, 6},
        {0, 1, 5}, {0, 5, 4},
        {2, 6, 7}, {2, 7, 3},
        {0, 4, 6}, {0, 6, 2},
        {1, 3, 7}, {1, 7, 5},
    };

    for (int i = 0; i < 12; i++) {
        if (!VK_Shadow_AddTriangle(corners[tris[i][0]],
                                   corners[tris[i][1]],
                                   corners[tris[i][2]])) {
            return false;
        }
    }
    return true;
}

static void VK_Shadow_RecordLightPage(const shadow_view_desc_t *view)
{
    if (view->view_type == SHADOW_VIEW_SUN_CASCADE) {
        if (vk_shadow.uniform.sun[0] < 0.0f ||
            (float)view->page.index < vk_shadow.uniform.sun[0]) {
            vk_shadow.uniform.sun[0] = (float)view->page.index;
        }
        vk_shadow.uniform.sun[1] += 1.0f;
        vk_shadow.uniform.sun[2] =
            vk_shadow.uniform.pages[view->page.index].params[2];
        vk_shadow.uniform.sun[3] = VK_SHADOW_DEFAULT_STRENGTH;
        return;
    }

    if (view->light_index >= MAX_DLIGHTS) {
        return;
    }

    vk_shadow_light_pages_t *light = &vk_shadow.lights[view->light_index];
    light->view_type = view->view_type;
    if (view->view_type == SHADOW_VIEW_POINT_FACE &&
        view->face >= 0 && view->face < SHADOW_FRONTEND_POINT_FACES) {
        light->pages[view->face] = (int)view->page.index;
    } else {
        light->pages[0] = (int)view->page.index;
    }

    int count = 0;
    for (int i = 0; i < SHADOW_FRONTEND_POINT_FACES; i++) {
        if (light->pages[i] >= 0) {
            count++;
        }
    }
    light->page_count = count;
}

static void VK_Shadow_RegisterView(const shadow_view_desc_t *view)
{
    if (!VK_Shadow_ValidView(view) || !vk_shadow.resources_ok) {
        return;
    }

    mat4_t view_matrix;
    mat4_t proj_matrix;
    renderer_view_push_t push;
    VK_Shadow_BuildPush(view, &push);
    memcpy(view_matrix, push.view, sizeof(view_matrix));
    memcpy(proj_matrix, push.proj, sizeof(proj_matrix));

    vk_shadow_uniform_page_t *page =
        &vk_shadow.uniform.pages[view->page.index];
    VK_Shadow_MultiplyMatrix(proj_matrix, view_matrix, page->matrix);
    page->params[0] = (float)view->filter_family;
    page->params[1] = 1.0f / (float)max(vk_shadow.resolution, 1);
    page->params[2] = VK_Shadow_ViewReceiverBias(view);
    page->params[3] = VK_Shadow_ViewNormalOffset(view);

    vk_shadow.max_active_page = max(vk_shadow.max_active_page,
                                    (int)view->page.index);
    VK_Shadow_RecordLightPage(view);
}

bool VK_Shadow_Init(vk_context_t *ctx)
{
    memset(&vk_shadow, 0, sizeof(vk_shadow));
    if (!ctx) {
        return false;
    }
    vk_shadow.ctx = ctx;
    vk_shadow.initialized = true;
    vk_shadow.depth_format = VK_Shadow_ChooseDepthFormat();
    if (vk_shadow.depth_format == VK_FORMAT_UNDEFINED) {
        Com_EPrintf("Vulkan shadow: no depth format supports attachment sampling\n");
        memset(&vk_shadow, 0, sizeof(vk_shadow));
        return false;
    }
    vk_shadow.moment_format = VK_Shadow_ChooseMomentFormat();
    if (vk_shadow.moment_format == VK_FORMAT_UNDEFINED) {
        Com_EPrintf("Vulkan shadow: no color format supports moment pages\n");
        memset(&vk_shadow, 0, sizeof(vk_shadow));
        return false;
    }
    vk_shadow.moment_mips_supported =
        VK_Shadow_ColorFormatMipBlitUsable(vk_shadow.moment_format);
    if (!VK_Shadow_CreateDescriptors()) {
        VK_Shadow_DestroyResources();
        VK_Shadow_DestroyDescriptors();
        memset(&vk_shadow, 0, sizeof(vk_shadow));
        return false;
    }
    memset(&vk_shadow.uniform, 0, sizeof(vk_shadow.uniform));
    vk_shadow.uniform.sun[0] = -1.0f;
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (!VK_Shadow_CreateUniformBuffer(&vk_shadow.frame_resources[i])) {
            VK_Shadow_DestroyResources();
            for (uint32_t j = 0; j < VK_MAX_FRAMES_IN_FLIGHT; ++j) {
                VK_Shadow_DestroyUniformBuffer(&vk_shadow.frame_resources[j]);
            }
            VK_Shadow_DestroyDescriptors();
            memset(&vk_shadow, 0, sizeof(vk_shadow));
            return false;
        }
        memcpy(vk_shadow.frame_resources[i].uniform_mapped,
               &vk_shadow.uniform, sizeof(vk_shadow.uniform));
    }
    if (!VK_Shadow_EnsureResources(64, SHADOW_STORAGE_DEPTH_COMPARE, NULL)) {
        VK_Shadow_DestroyResources();
        for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
            VK_Shadow_DestroyUniformBuffer(&vk_shadow.frame_resources[i]);
        }
        VK_Shadow_DestroyDescriptors();
        memset(&vk_shadow, 0, sizeof(vk_shadow));
        return false;
    }
    vk_shadow.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void VK_Shadow_Shutdown(vk_context_t *ctx)
{
    (void)ctx;
    if (!vk_shadow.initialized) {
        return;
    }
    if (vk_shadow.ctx && vk_shadow.ctx->device) {
        vkDeviceWaitIdle(vk_shadow.ctx->device);
    }
    VK_Shadow_FreeWorldCache();
    VK_Shadow_DestroyResources();
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_Shadow_DestroyVertexBuffer(&vk_shadow.frame_resources[i]);
        VK_Shadow_DestroyUniformBuffer(&vk_shadow.frame_resources[i]);
    }
    VK_Shadow_DestroyDescriptors();
    free(vk_shadow.vertices);
    memset(&vk_shadow, 0, sizeof(vk_shadow));
}

VkDescriptorSetLayout VK_Shadow_GetDescriptorSetLayout(void)
{
    return vk_shadow.descriptor_set_layout;
}

VkDescriptorSet VK_Shadow_GetDescriptorSet(void)
{
    vk_shadow_frame_resources_t *frame = VK_Shadow_CurrentFrameResources();
    return frame ? frame->descriptor_set : VK_NULL_HANDLE;
}

bool VK_Shadow_HasActiveReceiverLighting(void)
{
    // The receiver shader's expensive lighting paths are relevant only while
    // a sun page is active or the current refdef supplied dynamic lights.
    // This is queried after ShadowFrontend_BuildFrame and
    // VK_Shadow_UpdateDlights have populated the frame UBO.
    return vk_shadow.sun_active || vk_shadow.uniform.dlight_count[0] > 0.0f;
}

bool VK_Shadow_HasActiveSurfaceFog(void)
{
    // The opaque receiver shaders invoke apply_fog(..., false), so a
    // sky-only fog flag cannot affect them. The query runs after
    // VK_Shadow_UpdateDlights has refreshed the current frame UBO and lets
    // static-light receiver pipelines omit all fog work only when global and
    // height fog are both inactive.
    const uint32_t flags = (uint32_t)(vk_shadow.uniform.fog_params[3] + 0.5f);
    return (flags & (VK_FOG_GLOBAL | VK_FOG_HEIGHT)) != 0;
}

static void VK_Shadow_FillDlightPages(int source_index,
                                      vk_shadow_uniform_dlight_t *out)
{
    Vector4Set(out->shadow_pages0, -1.0f, -1.0f, -1.0f, -1.0f);
    Vector4Set(out->shadow_pages1, -1.0f, -1.0f, 0.0f, 0.0f);

    if (source_index < 0 || source_index >= MAX_DLIGHTS) {
        return;
    }

    const vk_shadow_light_pages_t *light = &vk_shadow.lights[source_index];
    if (light->page_count <= 0) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        out->shadow_pages0[i] = (float)light->pages[i];
    }
    out->shadow_pages1[0] = (float)light->pages[4];
    out->shadow_pages1[1] = (float)light->pages[5];
    out->shadow_pages1[2] =
        light->view_type == SHADOW_VIEW_POINT_FACE ? 1.0f : 2.0f;
    out->shadow_pages1[3] = (float)light->page_count;
}

static void VK_Shadow_UpdateFog(const refdef_t *fd)
{
    if (!vk_fog) {
        vk_fog = Cvar_Get("vk_fog", "1", 0);
    }

    memset(vk_shadow.uniform.fog_color_density, 0,
           sizeof(vk_shadow.uniform.fog_color_density));
    memset(vk_shadow.uniform.heightfog_start, 0,
           sizeof(vk_shadow.uniform.heightfog_start));
    memset(vk_shadow.uniform.heightfog_end, 0,
           sizeof(vk_shadow.uniform.heightfog_end));
    memset(vk_shadow.uniform.fog_params, 0,
           sizeof(vk_shadow.uniform.fog_params));

    if (!fd || !vk_fog || vk_fog->integer <= 0) {
        return;
    }

    uint32_t flags = 0;
    if (fd->fog.density > 0.0f) {
        VectorCopy(fd->fog.color, vk_shadow.uniform.fog_color_density);
        vk_shadow.uniform.fog_color_density[3] = fd->fog.density * (1.0f / 64.0f);
        flags |= VK_FOG_GLOBAL;
    }
    if (fd->heightfog.density > 0.0f && fd->heightfog.falloff > 0.0f) {
        VectorCopy(fd->heightfog.start.color, vk_shadow.uniform.heightfog_start);
        vk_shadow.uniform.heightfog_start[3] = fd->heightfog.start.dist;
        VectorCopy(fd->heightfog.end.color, vk_shadow.uniform.heightfog_end);
        vk_shadow.uniform.heightfog_end[3] = fd->heightfog.end.dist;
        vk_shadow.uniform.fog_params[0] = fd->heightfog.density;
        vk_shadow.uniform.fog_params[1] = fd->heightfog.falloff;
        flags |= VK_FOG_HEIGHT;
    }
    if (fd->fog.sky_factor > 0.0f) {
        vk_shadow.uniform.fog_params[2] = fd->fog.sky_factor;
        flags |= VK_FOG_SKY;
    }
    vk_shadow.uniform.fog_params[3] = (float)flags;
}

void VK_Shadow_UpdateDlights(const refdef_t *fd)
{
    for (int i = 0; i < MAX_DLIGHTS; i++) {
        memset(&vk_shadow.uniform.dlights[i], 0,
               sizeof(vk_shadow.uniform.dlights[i]));
        VK_Shadow_FillDlightPages(-1, &vk_shadow.uniform.dlights[i]);
    }
    vk_shadow.uniform.dlight_count[0] = 0.0f;
    if (fd) {
        VectorCopy(fd->vieworg, vk_shadow.uniform.view_origin);
        vk_shadow.uniform.view_origin[3] = 1.0f;
    }
    VK_Shadow_UpdateFog(fd);

    if (!fd || !fd->dlights) {
        VK_Shadow_UploadUniform();
        return;
    }

    int count = min(max(fd->num_dlights, 0), MAX_DLIGHTS);
    for (int i = 0; i < count; i++) {
        const dlight_t *dl = &fd->dlights[i];
        vk_shadow_uniform_dlight_t *out = &vk_shadow.uniform.dlights[i];

        Vector4Set(out->position_radius,
                   dl->origin[0], dl->origin[1], dl->origin[2], dl->radius);
        Vector4Set(out->color_intensity,
                   dl->color[0], dl->color[1], dl->color[2], dl->intensity);
        Vector4Set(out->cone,
                   dl->cone[0], dl->cone[1], dl->cone[2], dl->conecos);
        VK_Shadow_FillDlightPages(i, out);
    }
    vk_shadow.uniform.dlight_count[0] = (float)count;
    VK_Shadow_UploadUniform();
}

void VK_Shadow_BeginFrame(void *userdata,
                          const shadow_frontend_policy_t *policy)
{
    (void)userdata;
    vk_shadow.frame_active = policy && policy->enabled;
    // Pages rendered before a mid-frame reallocation were destroyed with the
    // old image while their resident entries stayed clean; keep reporting
    // "allocated" for one extra frame so every view re-renders once.
    vk_shadow.reallocated_last_frame = vk_shadow.reallocated_this_frame;
    vk_shadow.reallocated_this_frame = false;
    vk_shadow.policy = policy ? *policy : (shadow_frontend_policy_t){0};
    vk_shadow_frame_resources_t *frame = VK_Shadow_CurrentFrameResources();
    if (frame) {
        frame->vertex_upload_bytes = 0;
        frame->vertex_upload_recorded = false;
    }
    vk_shadow.vertex_count = 0;
    vk_shadow.job_count = 0;
    vk_shadow.max_active_page = -1;
    vk_shadow.sun_active = false;
    vk_shadow.world_faces_considered = 0;
    vk_shadow.world_faces_submitted = 0;
    memset(&vk_shadow.uniform, 0, sizeof(vk_shadow.uniform));
    vk_shadow.uniform.global[1] = VK_SHADOW_DEFAULT_STRENGTH;
    vk_shadow.uniform.sun[0] = -1.0f;
    vk_shadow.uniform.dlight_count[1] = 1.0f;
    vk_shadow.uniform.dlight_count[3] = 1.0f;
    vk_shadow.uniform.moment_tuning[0] = VK_SHADOW_MOMENT_MIN_VARIANCE;
    vk_shadow.uniform.moment_tuning[1] = VK_SHADOW_EVSM_EXPONENT;
    vk_shadow.uniform.moment_tuning[2] = 1.0f;
    vk_shadow.uniform.moment_tuning[3] = VK_World_Fullbright() ? 1.0f : 0.0f;
    vk_shadow.uniform.glowmap_tuning[0] = 1.0f;
    for (int i = 0; i < MAX_DLIGHTS; i++) {
        for (int j = 0; j < SHADOW_FRONTEND_POINT_FACES; j++) {
            vk_shadow.lights[i].pages[j] = -1;
        }
        vk_shadow.lights[i].page_count = 0;
        vk_shadow.lights[i].view_type = SHADOW_VIEW_POINT_FACE;
    }
}

bool VK_Shadow_EnsurePage(void *userdata, const shadow_view_desc_t *view)
{
    (void)userdata;
    if (!vk_shadow.frame_active || !VK_Shadow_ValidView(view)) {
        return false;
    }
    bool allocated = false;
    if (!VK_Shadow_EnsureResources(view->resolution, view->storage_family,
                                   &allocated)) {
        return false;
    }
    VK_Shadow_RegisterView(view);
    return allocated || vk_shadow.reallocated_this_frame ||
           vk_shadow.reallocated_last_frame;
}

bool VK_Shadow_RenderView(void *userdata, const shadow_view_desc_t *view,
                          const shadow_caster_t *casters,
                          const int *caster_indices, int caster_count)
{
    (void)userdata;
    if (!vk_shadow.frame_active || !vk_shadow.resources_ok ||
        !VK_Shadow_ValidView(view) || caster_count < 0 ||
        vk_shadow.job_count >= VK_SHADOW_MAX_PAGES) {
        return false;
    }

    vk_shadow_job_t *job = &vk_shadow.jobs[vk_shadow.job_count];
    memset(job, 0, sizeof(*job));
    job->page = view->page.index;
    job->first_vertex = vk_shadow.vertex_count;
    job->slope_bias = VK_Shadow_ViewSlopeBias(view);
    job->bias_scale = VK_Shadow_ViewConstantBias(view);
    job->storage_family = view->storage_family;
    VK_Shadow_BuildPush(view, &job->push.view);
    job->push.filter = (float)view->filter_family;
    job->push.evsm_exponent = VK_SHADOW_EVSM_EXPONENT;

    if (!VK_Shadow_AddWorldDepth(view)) {
        vk_shadow.vertex_count = job->first_vertex;
        memset(job, 0, sizeof(*job));
        return false;
    }
    for (int i = 0; i < caster_count; i++) {
        int caster_index = caster_indices ? caster_indices[i] : -1;
        if (caster_index < 0 || !casters) {
            continue;
        }
        const shadow_caster_t *caster = &casters[caster_index];
        // The frontend culls casters per light; cube faces of a point light
        // would otherwise re-skin and re-submit every caster six times.
        if (!VK_Shadow_ViewTouchesBounds(view, caster->bounds[0],
                                         caster->bounds[1])) {
            continue;
        }
        const bsp_t *world_bsp = VK_World_GetBsp();
        uint32_t caster_first_vertex = vk_shadow.vertex_count;
        if (!VK_Entity_EmitShadowCaster(&caster->entity, NULL,
                                        world_bsp,
                                        VK_Shadow_EmitCasterTriangle, NULL)) {
            vk_shadow.vertex_count = job->first_vertex;
            memset(job, 0, sizeof(*job));
            return false;
        }
        if (vk_shadow.vertex_count == caster_first_vertex &&
            (caster->flags & RF_CASTSHADOW) &&
            !VK_Shadow_EmitBoundsCaster(caster)) {
            vk_shadow.vertex_count = job->first_vertex;
            memset(job, 0, sizeof(*job));
            return false;
        }
    }
    job->vertex_count = vk_shadow.vertex_count - job->first_vertex;
    if (!job->vertex_count) {
        vk_shadow.vertex_count = job->first_vertex;
        memset(job, 0, sizeof(*job));
        return false;
    }

    vk_shadow.job_count++;
    return true;
}

void VK_Shadow_EndFrame(void *userdata,
                        const shadow_frontend_stats_t *stats)
{
    (void)userdata;
    (void)stats;

    if (vk_shadow.max_active_page >= 0 && vk_shadow.resources_ok) {
        vk_shadow.uniform.global[0] = (float)(vk_shadow.max_active_page + 1);
        vk_shadow.uniform.global[1] = VK_SHADOW_DEFAULT_STRENGTH;
        vk_shadow.uniform.global[2] =
            Q_clipf(vk_shadow.policy.softness, 0.25f, 4.0f);
        vk_shadow.uniform.global[3] = (float)vk_shadow.policy.filter_family;
    } else {
        vk_shadow.uniform.global[0] = 0.0f;
        vk_shadow.uniform.global[2] = 0.0f;
        vk_shadow.uniform.global[3] = 0.0f;
        vk_shadow.uniform.sun[0] = -1.0f;
    }
    vk_shadow.sun_active =
        vk_shadow.uniform.global[0] > 0.0f &&
        vk_shadow.uniform.sun[0] >= 0.0f &&
        vk_shadow.uniform.sun[1] > 0.0f;
    // y/z/w carry the lightmap modulate, brightness add, and entity modulate
    // for the world and entity receiver shaders; x stays the dlight count.
    vk_shadow.uniform.dlight_count[1] = VK_World_LightmapModulate();
    vk_shadow.uniform.dlight_count[2] = VK_World_LightmapAdd();
    vk_shadow.uniform.dlight_count[3] = VK_World_EntityModulate();
    // z/w carry legacy texture intensity and the global fullbright state.
    // Fullbright is evaluated in the world fragment shader so changing the
    // cvar never rebuilds or re-uploads static world geometry.
    vk_shadow.uniform.moment_tuning[2] = VK_World_Intensity();
    vk_shadow.uniform.moment_tuning[3] = VK_World_Fullbright() ? 1.0f : 0.0f;
    vk_shadow.uniform.glowmap_tuning[0] = VK_World_GlowmapIntensity();
    VK_Shadow_UploadUniform();

    if (!vk_shadow.vertex_count) {
        return;
    }

    size_t bytes;
    if (!vk_shadow.vertices ||
        !VK_Shadow_ArrayBytes((size_t)vk_shadow.vertex_count, sizeof(*vk_shadow.vertices),
                              &bytes, "vertex upload")) {
        vk_shadow.vertex_count = 0;
        vk_shadow.job_count = 0;
        return;
    }

    vk_shadow_frame_resources_t *frame = VK_Shadow_CurrentFrameResources();
    if (!VK_Shadow_EnsureVertexBuffer(bytes) || !frame ||
        !frame->vertex_staging_mapped) {
        vk_shadow.vertex_count = 0;
        vk_shadow.job_count = 0;
        return;
    }
    memcpy(frame->vertex_staging_mapped, vk_shadow.vertices, bytes);
    frame->vertex_upload_bytes = bytes;
    VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_SHADOW, bytes);
}

void VK_Shadow_RecordUploads(VkCommandBuffer cmd)
{
    vk_shadow_frame_resources_t *frame = VK_Shadow_CurrentFrameResources();
    if (!cmd || !vk_shadow.initialized || !frame ||
        !frame->vertex_upload_bytes || !frame->vertex_staging_buffer ||
        !frame->vertex_buffer || frame->vertex_upload_recorded) {
        return;
    }

    const VkBufferCopy copy = { .size = frame->vertex_upload_bytes };
    vkCmdCopyBuffer(cmd, frame->vertex_staging_buffer, frame->vertex_buffer,
                    1, &copy);
    VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = frame->vertex_buffer,
        .size = frame->vertex_upload_bytes,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
                         0, NULL, 1, &barrier, 0, NULL);
    frame->vertex_upload_recorded = true;
}

const char *VK_Shadow_DescribeMaterialization(void *userdata)
{
    (void)userdata;
    return vk_shadow.resources_ok
        ? va("%s 2d-array format=%d pages=%d size=%d mips=%d world=%d/%d",
             vk_shadow.storage_family == SHADOW_STORAGE_MOMENT
                 ? "moment"
                 : "depth-compare",
             (int)(vk_shadow.storage_family == SHADOW_STORAGE_MOMENT
                       ? vk_shadow.moment_format
                       : vk_shadow.depth_format),
             vk_shadow.max_active_page + 1, vk_shadow.resolution,
             vk_shadow.storage_family == SHADOW_STORAGE_MOMENT
                 ? vk_shadow.mip_levels
                 : 1,
             vk_shadow.world_faces_submitted,
             vk_shadow.world_faces_considered)
        : "unallocated";
}

static void VK_Shadow_Barrier(VkCommandBuffer cmd, VkImageLayout old_layout,
                              VkImageLayout new_layout,
                              VkAccessFlags src_access,
                              VkAccessFlags dst_access,
                              VkPipelineStageFlags src_stage,
                              VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk_shadow.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = VK_SHADOW_MAX_PAGES,
        },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_Shadow_MomentBarrierRange(VkCommandBuffer cmd,
                                         uint32_t base_mip,
                                         uint32_t level_count,
                                         VkImageLayout old_layout,
                                         VkImageLayout new_layout,
                                         VkAccessFlags src_access,
                                         VkAccessFlags dst_access,
                                         VkPipelineStageFlags src_stage,
                                         VkPipelineStageFlags dst_stage);

static void VK_Shadow_MomentBarrier(VkCommandBuffer cmd,
                                    VkImageLayout old_layout,
                                    VkImageLayout new_layout,
                                    VkAccessFlags src_access,
                                    VkAccessFlags dst_access,
                                    VkPipelineStageFlags src_stage,
                                    VkPipelineStageFlags dst_stage)
{
    VK_Shadow_MomentBarrierRange(cmd, 0, (uint32_t)vk_shadow.mip_levels,
                                 old_layout, new_layout,
                                 src_access, dst_access,
                                 src_stage, dst_stage);
}

static void VK_Shadow_MomentBarrierRange(VkCommandBuffer cmd,
                                         uint32_t base_mip,
                                         uint32_t level_count,
                                         VkImageLayout old_layout,
                                         VkImageLayout new_layout,
                                         VkAccessFlags src_access,
                                         VkAccessFlags dst_access,
                                         VkPipelineStageFlags src_stage,
                                         VkPipelineStageFlags dst_stage)
{
    if (!vk_shadow.moment_image) {
        return;
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk_shadow.moment_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = base_mip,
            .levelCount = level_count,
            .baseArrayLayer = 0,
            .layerCount = VK_SHADOW_MAX_PAGES,
        },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_Shadow_GenerateMomentMips(VkCommandBuffer cmd)
{
    if (!vk_shadow.moment_image || vk_shadow.mip_levels <= 1) {
        VK_Shadow_MomentBarrier(cmd,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        return;
    }

    int src_size = vk_shadow.resolution;
    for (uint32_t level = 1; level < (uint32_t)vk_shadow.mip_levels; level++) {
        VK_Shadow_MomentBarrierRange(
            cmd, level - 1, 1,
            level == 1 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                       : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            level == 1 ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                       : VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            level == 1 ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                       : VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        VK_Shadow_MomentBarrierRange(
            cmd, level, 1,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        int dst_size = max(src_size >> 1, 1);
        VkImageBlit blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = level - 1,
                .baseArrayLayer = 0,
                .layerCount = VK_SHADOW_MAX_PAGES,
            },
            .srcOffsets = {
                { 0, 0, 0 },
                { src_size, src_size, 1 },
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = level,
                .baseArrayLayer = 0,
                .layerCount = VK_SHADOW_MAX_PAGES,
            },
            .dstOffsets = {
                { 0, 0, 0 },
                { dst_size, dst_size, 1 },
            },
        };
        vkCmdBlitImage(cmd,
                       vk_shadow.moment_image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       vk_shadow.moment_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        VK_Shadow_MomentBarrierRange(
            cmd, level - 1, 1,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        src_size = dst_size;
    }

    VK_Shadow_MomentBarrierRange(
        cmd, (uint32_t)vk_shadow.mip_levels - 1, 1,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VK_Shadow_Record(VkCommandBuffer cmd)
{
    if (!vk_shadow.initialized || !vk_shadow.resources_ok || !cmd ||
        vk_shadow.resolution <= 0 ||
        !VK_Shadow_ValidStorageFamily(vk_shadow.storage_family)) {
        return;
    }

    vk_shadow_frame_resources_t *frame = VK_Shadow_CurrentFrameResources();
    if (!frame || !frame->vertex_buffer || !frame->vertex_upload_recorded ||
        !vk_shadow.vertex_count || !vk_shadow.job_count) {
        if (!vk_shadow.layout_initialized) {
            VK_Shadow_Barrier(cmd, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                              0,
                              VK_ACCESS_SHADER_READ_BIT,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vk_shadow.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            vk_shadow.layout_initialized = true;
        }
        return;
    }

    VkImageLayout old_layout = vk_shadow.layout_initialized
        ? vk_shadow.layout
        : VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags old_access = vk_shadow.layout_initialized
        ? VK_ACCESS_SHADER_READ_BIT
        : 0;
    VkPipelineStageFlags old_stage = vk_shadow.layout_initialized
        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VK_Shadow_Barrier(cmd, old_layout,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      old_access,
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      old_stage,
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    if (vk_shadow.storage_family == SHADOW_STORAGE_MOMENT) {
        VkImageLayout old_moment_layout =
            vk_shadow.layout_initialized
                ? vk_shadow.moment_layout
                : VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags old_moment_access =
            vk_shadow.layout_initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
        VkPipelineStageFlags old_moment_stage =
            vk_shadow.layout_initialized
                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VK_Shadow_MomentBarrier(cmd, old_moment_layout,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                old_moment_access,
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                old_moment_stage,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }

    // Unlike the swapchain passes, shadow pages must NOT use the negated
    // viewport trick: the receiver reconstructs uv as ndc * 0.5 + 0.5, which
    // matches GL's window/texel mapping (ndc.y = -1 -> texel row 0). A
    // negative-height viewport would mirror every page vertically relative
    // to that lookup.
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)vk_shadow.resolution,
        .height = (float)vk_shadow.resolution,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = { (uint32_t)vk_shadow.resolution,
                    (uint32_t)vk_shadow.resolution },
    };
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_shadow.pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->vertex_buffer, &offset);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkClearValue clear_values[2] = {
        { .color = { .float32 = { 1.0f, 1.0f, 0.0f, 1.0f } } },
        { .depthStencil = { 1.0f, 0 } },
    };
    const float evsm_clear_moment = expf(VK_SHADOW_EVSM_EXPONENT);
    VkClearValue clear = {
        .depthStencil = { 1.0f, 0 },
    };
    for (uint32_t i = 0; i < vk_shadow.job_count; i++) {
        const vk_shadow_job_t *job = &vk_shadow.jobs[i];
        if (!job->vertex_count || job->page >= VK_SHADOW_MAX_PAGES ||
            !vk_shadow.framebuffers[job->page]) {
            continue;
        }

        if ((int)(job->push.filter + 0.5f) == SHADOW_FILTER_EVSM) {
            clear_values[0].color.float32[0] = evsm_clear_moment;
            clear_values[0].color.float32[1] =
                evsm_clear_moment * evsm_clear_moment;
        } else {
            clear_values[0].color.float32[0] = 1.0f;
            clear_values[0].color.float32[1] = 1.0f;
        }

        VkRenderPassBeginInfo begin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = vk_shadow.render_pass,
            .framebuffer = vk_shadow.framebuffers[job->page],
            .renderArea = {
                .offset = { 0, 0 },
                .extent = { (uint32_t)vk_shadow.resolution,
                            (uint32_t)vk_shadow.resolution },
            },
            .clearValueCount =
                vk_shadow.storage_family == SHADOW_STORAGE_MOMENT ? 2u : 1u,
            .pClearValues = vk_shadow.storage_family == SHADOW_STORAGE_MOMENT
                ? clear_values
                : &clear,
        };
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetDepthBias(cmd, job->bias_scale, 0.0f, job->slope_bias);
        vkCmdPushConstants(cmd, vk_shadow.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(job->push), &job->push);
        vkCmdDraw(cmd, job->vertex_count, 1, job->first_vertex, 0);
        VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_SHADOW, job->vertex_count, 0);
        vkCmdEndRenderPass(cmd);
    }

    VK_Shadow_Barrier(cmd,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (vk_shadow.storage_family == SHADOW_STORAGE_MOMENT) {
        VK_Shadow_GenerateMomentMips(cmd);
        vk_shadow.moment_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    vk_shadow.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    vk_shadow.layout_initialized = true;
}
