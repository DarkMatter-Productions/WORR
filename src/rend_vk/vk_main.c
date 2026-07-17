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

#include "vk_local.h"
#include "vk_debug.h"
#include "vk_entity.h"
#include "vk_postprocess.h"
#include "vk_shadow.h"
#include "vk_ui.h"
#include "vk_world.h"
#include "common/utils.h"
#include "format/pcx.h"
#include "renderer/shadow_frontend.h"
#include "refresh/debug.h"
#include "refresh/refresh.h"

#include "stb_image_write.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if USE_SDL
#include <SDL3/SDL_vulkan.h>
#endif

#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif

#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR ((VkInstanceCreateFlags)0x00000001)
#endif

vk_state_t vk_state;
refcfg_t r_config;
uint32_t d_8to24table[256];
static cvar_t *vk_draw_world;
static cvar_t *vk_draw_entities;
static cvar_t *vk_draw_ui;
static cvar_t *vk_showtearing;
static cvar_t *vk_damageblend_frac;
static cvar_t *vk_screenshot_dir;
static cvar_t *vk_resolutionscale;
static cvar_t *vk_resolutionscale_aggressive;
static cvar_t *vk_resolutionscale_fixedscale_h;
static cvar_t *vk_resolutionscale_fixedscale_w;
static cvar_t *vk_resolutionscale_gooddrawtime;
static cvar_t *vk_resolutionscale_increasespeed;
static cvar_t *vk_resolutionscale_lowerspeed;
static cvar_t *vk_resolutionscale_numframesbeforelowering;
static cvar_t *vk_resolutionscale_numframesbeforeraising;
static cvar_t *vk_resolutionscale_targetdrawtime;
static uint32_t vk_showtearing_frame;
static shadow_frontend_state_t vk_shadow_frontend;
static shadow_frontend_cvars_t vk_shadow_frontend_cvars;
static bool vk_shadow_frontend_cvars_registered;

// Swapchain readback for the screenshot commands. A request latches in
// vk_screenshot_pending; the next presented frame records a copy of its
// swapchain image into a host-visible buffer and writes the requested format after the
// frame fence signals.
static char vk_screenshot_path[MAX_OSPATH];
static bool vk_screenshot_tga;
static bool vk_screenshot_pending;
static bool vk_screenshot_armed;
static bool vk_screenshot_supported;
static VkBuffer vk_screenshot_buffer;
static VkDeviceMemory vk_screenshot_memory;
static VkDeviceSize vk_screenshot_capacity;
static bool vk_dof_supported;
static uint64_t vk_frame_begin_us;

#define VK_RESOLUTION_SCALE_MIN 0.25f
#define VK_RESOLUTION_SCALE_MAX 1.0f

static float vk_resolutionscale_current_w = 1.0f;
static float vk_resolutionscale_current_h = 1.0f;
static float vk_resolutionscale_session_draw_ms;
static unsigned vk_resolutionscale_draw_samples;
static uint64_t vk_resolutionscale_last_gpu_sample_id;
static int vk_resolutionscale_good_frames;
static int vk_resolutionscale_bad_frames;
static int vk_resolutionscale_last_mode = -1;
static int vk_resolutionscale_last_width;
static int vk_resolutionscale_last_height;

// Preserve sub-millisecond CPU timing as the adaptive-scale fallback when a
// device cannot provide Vulkan timestamp queries. Sys_Milliseconds() is
// QPC-backed on Windows but its integer result makes ordinary Vulkan submit
// work look like 0 ms.
static uint64_t VK_HighResMicroseconds(void) {
#if defined(_WIN32)
  static LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  if (!frequency.QuadPart) {
    QueryPerformanceFrequency(&frequency);
  }
  QueryPerformanceCounter(&counter);
  return (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000ULL +
         (uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000ULL /
             (uint64_t)frequency.QuadPart;
#else
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_nsec / 1000ULL;
#endif
}

static void VK_ResetResolutionScaleHistory(void) {
  uint64_t gpu_sample_id = 0;
  vk_resolutionscale_session_draw_ms = 0.0f;
  vk_resolutionscale_draw_samples = 0;
  VK_Debug_GetLastGpuFrameTime(NULL, &gpu_sample_id);
  vk_resolutionscale_last_gpu_sample_id = gpu_sample_id;
  vk_resolutionscale_good_frames = 0;
  vk_resolutionscale_bad_frames = 0;
}

static float VK_ClampResolutionScale(float scale) {
  return Q_clipf(scale, VK_RESOLUTION_SCALE_MIN, VK_RESOLUTION_SCALE_MAX);
}

static int VK_GetResolutionScaleMode(void) {
  return vk_resolutionscale ? Cvar_ClampInteger(vk_resolutionscale, 0, 2) : 0;
}

static void VK_UpdateResolutionScale(int width, int height) {
  const int mode = VK_GetResolutionScaleMode();
  if (mode != vk_resolutionscale_last_mode) {
    vk_resolutionscale_last_mode = mode;
    vk_resolutionscale_current_w = 1.0f;
    vk_resolutionscale_current_h = 1.0f;
    VK_ResetResolutionScaleHistory();
  }
  if (width != vk_resolutionscale_last_width ||
      height != vk_resolutionscale_last_height) {
    vk_resolutionscale_last_width = width;
    vk_resolutionscale_last_height = height;
    VK_ResetResolutionScaleHistory();
  }

  if (mode == 0) {
    vk_resolutionscale_current_w = 1.0f;
    vk_resolutionscale_current_h = 1.0f;
  } else if (mode == 1) {
    vk_resolutionscale_current_w =
        Cvar_ClampValue(vk_resolutionscale_fixedscale_w,
                        VK_RESOLUTION_SCALE_MIN, VK_RESOLUTION_SCALE_MAX);
    vk_resolutionscale_current_h =
        Cvar_ClampValue(vk_resolutionscale_fixedscale_h,
                        VK_RESOLUTION_SCALE_MIN, VK_RESOLUTION_SCALE_MAX);
    vk_resolutionscale_good_frames = 0;
    vk_resolutionscale_bad_frames = 0;
  } else {
    float target =
        Cvar_ClampValue(vk_resolutionscale_targetdrawtime, 0.0f, 1000.0f);
    float good =
        Cvar_ClampValue(vk_resolutionscale_gooddrawtime, 0.0f, 1000.0f);
    float increase =
        Cvar_ClampValue(vk_resolutionscale_increasespeed, 0.0f, 1.0f);
    float lower = Cvar_ClampValue(vk_resolutionscale_lowerspeed, 0.0f, 1.0f);
    int raise_frames =
        Cvar_ClampInteger(vk_resolutionscale_numframesbeforeraising, 1, 10000);
    int lower_frames =
        Cvar_ClampInteger(vk_resolutionscale_numframesbeforelowering, 1, 10000);
    if (good > target) {
      good = target;
    }
    if (vk_resolutionscale_aggressive &&
        vk_resolutionscale_aggressive->integer) {
      increase *= 2.0f;
      lower *= 2.0f;
      raise_frames = max(1, raise_frames / 2);
      lower_frames = max(1, lower_frames / 2);
    }
    if (vk_resolutionscale_draw_samples > 0) {
      if (vk_resolutionscale_session_draw_ms <= good) {
        vk_resolutionscale_good_frames++;
        vk_resolutionscale_bad_frames = 0;
      } else if (vk_resolutionscale_session_draw_ms >= target) {
        vk_resolutionscale_bad_frames++;
        vk_resolutionscale_good_frames = 0;
      } else {
        vk_resolutionscale_good_frames = 0;
        vk_resolutionscale_bad_frames = 0;
      }
      if (vk_resolutionscale_bad_frames >= lower_frames) {
        vk_resolutionscale_current_w =
            VK_ClampResolutionScale(vk_resolutionscale_current_w - lower);
        vk_resolutionscale_current_h = vk_resolutionscale_current_w;
        vk_resolutionscale_bad_frames = 0;
      } else if (vk_resolutionscale_good_frames >= raise_frames) {
        vk_resolutionscale_current_w =
            VK_ClampResolutionScale(vk_resolutionscale_current_w + increase);
        vk_resolutionscale_current_h = vk_resolutionscale_current_w;
        vk_resolutionscale_good_frames = 0;
      }
    }
  }
  vk_resolutionscale_current_w =
      VK_ClampResolutionScale(vk_resolutionscale_current_w);
  vk_resolutionscale_current_h =
      VK_ClampResolutionScale(vk_resolutionscale_current_h);
}

static VkExtent2D VK_ResolutionScaleExtent(VkExtent2D output_extent) {
  VkExtent2D extent = {
      .width = max(
          1u, (uint32_t)(output_extent.width * vk_resolutionscale_current_w +
                         0.5f)),
      .height = max(
          1u, (uint32_t)(output_extent.height * vk_resolutionscale_current_h +
                         0.5f)),
  };
  return extent;
}

static void VK_RecordResolutionScaleTime(void) {
  float frame_ms;
  uint64_t gpu_sample_id = 0;
  if (VK_Debug_GetLastGpuFrameTime(&frame_ms, &gpu_sample_id)) {
    if (gpu_sample_id == vk_resolutionscale_last_gpu_sample_id) {
      return;
    }
    vk_resolutionscale_last_gpu_sample_id = gpu_sample_id;
  } else {
    // Timestamp-capable devices wait for a completed GPU sample instead
    // of mixing CPU submission time into the adaptive GPU controller.
    if (VK_Debug_GpuTimingSupported() || !vk_frame_begin_us ||
        !vk_state.initialized) {
      return;
    }
    frame_ms = (float)(VK_HighResMicroseconds() - vk_frame_begin_us) / 1000.0f;
  }
  if (!vk_resolutionscale_draw_samples) {
    vk_resolutionscale_session_draw_ms = frame_ms;
  } else {
    vk_resolutionscale_session_draw_ms =
        vk_resolutionscale_session_draw_ms * 0.9f + frame_ms * 0.1f;
  }
  vk_resolutionscale_draw_samples++;
}

static bool VK_ShadowResolveCasterBounds(void *userdata, const entity_t *ent,
                                         const bsp_t *world_bsp,
                                         vec3_t local_mins,
                                         vec3_t local_maxs)
{
    (void)userdata;
    if (!ent) {
        return false;
    }

    if (ent->model & BIT(31)) {
        int model_index = ~ent->model;
        if (!world_bsp || !world_bsp->models || model_index < 1 ||
            model_index >= world_bsp->nummodels) {
            return false;
        }

        const mmodel_t *model = &world_bsp->models[model_index];
        VectorCopy(model->mins, local_mins);
        VectorCopy(model->maxs, local_maxs);
        return true;
    }

    return VK_Entity_ModelBounds(ent->model, ent, local_mins, local_maxs);
}

static const char *VK_ShadowModelName(void *userdata, qhandle_t model,
                                      const bsp_t *world_bsp)
{
    (void)userdata;
    (void)world_bsp;
    if (!model || (model & BIT(31))) {
        return "";
    }
    return VK_Entity_ModelName(model);
}

static const shadow_backend_ops_t vk_shadow_backend_ops = {
    .backend_name = "vulkan",
    .supports_depth_compare_pages = true,
    .supports_moment_pages = true,
    .supports_cube_array_pages = false,
    .supports_array_2d_pages = true,
    .max_pages = VK_SHADOW_MAX_PAGES,
    .max_resolution = VK_SHADOW_MAX_RESOLUTION,
    .userdata = NULL,
    .begin_frame = VK_Shadow_BeginFrame,
    .resolve_caster_bounds = VK_ShadowResolveCasterBounds,
    .model_name = VK_ShadowModelName,
    .ensure_page = VK_Shadow_EnsurePage,
    .render_view = VK_Shadow_RenderView,
    .end_frame = VK_Shadow_EndFrame,
    .describe_materialization = VK_Shadow_DescribeMaterialization,
};

static bool VK_LoadPaletteFromColormap(void)
{
    byte *data = NULL;
    int len = FS_LoadFile("pics/colormap.pcx", (void **)&data);
    if (len < (int)sizeof(dpcx_t) || !data) {
        if (data) {
            FS_FreeFile(data);
        }
        return false;
    }

    const dpcx_t *pcx = (const dpcx_t *)data;
    bool valid = (pcx->manufacturer == 10 &&
                  pcx->version == 5 &&
                  pcx->encoding == 1 &&
                  pcx->bits_per_pixel == 8 &&
                  pcx->color_planes == 1 &&
                  len >= 768);
    if (!valid) {
        FS_FreeFile(data);
        return false;
    }

    const byte *src = data + len - 768;
    for (int i = 0; i < 255; i++, src += 3) {
        d_8to24table[i] = COLOR_U32_RGBA(src[0], src[1], src[2], 255);
    }
    d_8to24table[255] = COLOR_U32_RGBA(src[0], src[1], src[2], 0);

    FS_FreeFile(data);
    return true;
}

static void VK_InitPalette(void)
{
    if (VK_LoadPaletteFromColormap()) {
        return;
    }

    for (int i = 0; i < 255; i++) {
        d_8to24table[i] = COLOR_RGB(i, i, i).u32;
    }
    d_8to24table[255] = COLOR_RGBA(0, 0, 0, 0).u32;
    Com_WPrintf("Vulkan: using grayscale fallback palette; colormap.pcx not available\n");
}

static const char *VK_ResultString(VkResult result)
{
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "VK_UNKNOWN_ERROR";
    }
}

static bool VK_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }

    Com_SetLastError(va("Vulkan %s failed: %s", what, VK_ResultString(result)));
    return false;
}

static bool VK_ArrayBytes(size_t count, size_t item_size, size_t *out_size,
                          const char *what)
{
    if (!out_size) {
        Com_SetLastError("Vulkan: invalid allocation size output");
        return false;
    }

    if (item_size && count > SIZE_MAX / item_size) {
        Com_SetLastError(va("Vulkan: %s allocation size overflow", what));
        return false;
    }

    *out_size = count * item_size;
    return true;
}

static bool VK_ImageBytes(uint32_t width, uint32_t height, size_t bytes_per_pixel,
                          size_t *out_size, const char *what)
{
    if (!width || !height || !bytes_per_pixel) {
        Com_SetLastError(va("Vulkan: %s has zero dimensions", what));
        return false;
    }

    size_t pixel_count = 0;
    if (!VK_ArrayBytes((size_t)width, (size_t)height, &pixel_count, what)) {
        return false;
    }

    return VK_ArrayBytes(pixel_count, bytes_per_pixel, out_size, what);
}

static void *VK_AllocArray(size_t count, size_t item_size, const char *what)
{
    size_t bytes = 0;
    if (!VK_ArrayBytes(count, item_size, &bytes, what)) {
        return NULL;
    }

    void *memory = Z_TagMallocz(bytes, TAG_RENDERER);
    if (!memory) {
        Com_SetLastError(va("Vulkan: out of memory for %s", what));
        return NULL;
    }

    return memory;
}

static uint32_t VK_ClampU32(uint32_t value, uint32_t min_value, uint32_t max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static uint32_t VK_FindMemoryType(VkPhysicalDevice physical_device, uint32_t type_filter,
                                  VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & BIT(i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

static bool VK_IsDepthFormatSupported(VkPhysicalDevice device, VkFormat format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device, format, &props);
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
}

static bool VK_DepthFormatSupportsSampling(VkPhysicalDevice device, VkFormat format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device, format, &props);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (props.optimalTilingFeatures & required) == required;
}

static VkFormat VK_ChooseDepthFormat(VkPhysicalDevice device)
{
    // Prefer a combined depth/stencil attachment. Native alias-model outlines
    // use stencil masking just like the OpenGL renderer, while the depth-only
    // format remains a compatibility fallback for unusually limited devices.
    static const VkFormat candidates[] = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
    };

    for (size_t i = 0; i < q_countof(candidates); ++i) {
        if (VK_IsDepthFormatSupported(device, candidates[i])) {
            return candidates[i];
        }
    }

    return VK_FORMAT_UNDEFINED;
}

static bool VK_DepthFormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static const char *VK_SurfaceExtensionForPlatform(vid_native_platform_t platform)
{
    switch (platform) {
#if defined(_WIN32)
    case VID_NATIVE_WIN32:
        return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif
#if USE_X11
    case VID_NATIVE_X11:
        return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif
#if USE_WAYLAND
    case VID_NATIVE_WAYLAND:
        return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
#endif
    case VID_NATIVE_SDL:
    default:
        return NULL;
    }
}

static bool VK_InstanceHasExtension(const VkExtensionProperties *exts, uint32_t ext_count,
                                    const char *name)
{
    for (uint32_t i = 0; i < ext_count; ++i) {
        if (!strcmp(exts[i].extensionName, name))
            return true;
    }

    return false;
}

static bool VK_AddInstanceExtension(const char **extensions, uint32_t capacity, uint32_t *count,
                                    const char *name)
{
    for (uint32_t i = 0; i < *count; ++i) {
        if (!strcmp(extensions[i], name))
            return true;
    }

    if (*count >= capacity) {
        Com_SetLastError("Vulkan: instance extension list overflow");
        return false;
    }

    extensions[*count] = name;
    (*count)++;
    return true;
}

static bool VK_CollectInstanceExtensions(const VkExtensionProperties *exts, uint32_t ext_count,
                                         const char **extensions, uint32_t capacity,
                                         uint32_t *out_count,
                                         VkInstanceCreateFlags *out_flags)
{
    uint32_t enabled_count = 0;
    VkInstanceCreateFlags create_flags = 0;

#if USE_SDL
    if (vk_state.native_window.platform == VID_NATIVE_SDL) {
        Uint32 sdl_ext_count = 0;
        const char *const *sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
        if (!sdl_exts || !sdl_ext_count) {
            const char *error = SDL_GetError();
            Com_SetLastError(error && *error
                                 ? va("Vulkan: SDL failed to report instance extensions: %s", error)
                                 : "Vulkan: SDL failed to report instance extensions");
            return false;
        }

        for (Uint32 i = 0; i < sdl_ext_count; ++i) {
            const char *ext = sdl_exts[i];
            if (!VK_InstanceHasExtension(exts, ext_count, ext)) {
                Com_SetLastError(va("Vulkan: required SDL instance extension missing: %s", ext));
                return false;
            }
            if (!VK_AddInstanceExtension(extensions, capacity, &enabled_count, ext))
                return false;
        }
    } else
#endif
    {
        const char *surface_ext = VK_SurfaceExtensionForPlatform(vk_state.native_window.platform);
        if (!surface_ext) {
            Com_SetLastError("Vulkan: unsupported video driver for surface creation");
            return false;
        }

        if (!VK_InstanceHasExtension(exts, ext_count, VK_KHR_SURFACE_EXTENSION_NAME) ||
            !VK_InstanceHasExtension(exts, ext_count, surface_ext)) {
            Com_SetLastError("Vulkan: required instance extensions missing");
            return false;
        }

        if (!VK_AddInstanceExtension(extensions, capacity, &enabled_count,
                                     VK_KHR_SURFACE_EXTENSION_NAME) ||
            !VK_AddInstanceExtension(extensions, capacity, &enabled_count, surface_ext)) {
            return false;
        }
    }

    if (VK_InstanceHasExtension(exts, ext_count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        if (!VK_AddInstanceExtension(extensions, capacity, &enabled_count,
                                     VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            return false;
        }
        create_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    *out_count = enabled_count;
    *out_flags = create_flags;
    return true;
}

static void VK_DestroySurfaceHandle(vk_context_t *ctx)
{
    if (!ctx->instance || !ctx->surface)
        return;

#if USE_SDL
    if (vk_state.native_window.platform == VID_NATIVE_SDL)
        SDL_Vulkan_DestroySurface(ctx->instance, ctx->surface, NULL);
    else
#endif
        vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);

    ctx->surface = VK_NULL_HANDLE;
}

static bool VK_QueryNativeWindow(vid_native_window_t *out)
{
    if (!out) {
        Com_SetLastError("Vulkan: native window buffer missing");
        return false;
    }

    if (!vid || !vid->get_native_window) {
        Com_SetLastError("Vulkan: video driver does not expose native window handles");
        return false;
    }

    memset(out, 0, sizeof(*out));
    if (!vid->get_native_window(out)) {
        Com_SetLastError("Vulkan: video driver failed to provide native window handles");
        return false;
    }

    return true;
}

static bool VK_CreateInstance(void)
{
    uint32_t ext_count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkEnumerateInstanceExtensionProperties");
    }
    if (ext_count == 0) {
        Com_SetLastError("Vulkan: no instance extensions reported");
        return false;
    }

    VkExtensionProperties *exts = VK_AllocArray(ext_count, sizeof(*exts),
                                                "instance extensions");
    if (!exts) {
        return false;
    }

    result = vkEnumerateInstanceExtensionProperties(NULL, &ext_count, exts);
    if (result != VK_SUCCESS) {
        Z_Free(exts);
        return VK_Check(result, "vkEnumerateInstanceExtensionProperties");
    }

    const char *extensions[8];
    uint32_t enabled_ext_count = 0;
    VkInstanceCreateFlags create_flags = 0;
    bool have_extensions = VK_CollectInstanceExtensions(exts, ext_count, extensions,
                                                        q_countof(extensions), &enabled_ext_count,
                                                        &create_flags);
    Z_Free(exts);
    if (!have_extensions)
        return false;

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = PRODUCT,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "WORR",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = create_flags,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = enabled_ext_count,
        .ppEnabledExtensionNames = extensions,
    };

    return VK_Check(vkCreateInstance(&create_info, NULL, &vk_state.ctx.instance),
                    "vkCreateInstance");
}

static bool VK_CreateSurface(void)
{
    const vid_native_window_t *native = &vk_state.native_window;

    switch (native->platform) {
#if defined(_WIN32)
    case VID_NATIVE_WIN32: {
        VkWin32SurfaceCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = (HINSTANCE)native->handle.win32.hinstance,
            .hwnd = (HWND)native->handle.win32.hwnd,
        };
        return VK_Check(vkCreateWin32SurfaceKHR(vk_state.ctx.instance, &create_info, NULL,
                                                &vk_state.ctx.surface),
                        "vkCreateWin32SurfaceKHR");
    }
#endif
#if USE_X11
    case VID_NATIVE_X11: {
        VkXlibSurfaceCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = (Display *)native->handle.x11.display,
            .window = (Window)native->handle.x11.window,
        };
        return VK_Check(vkCreateXlibSurfaceKHR(vk_state.ctx.instance, &create_info, NULL,
                                               &vk_state.ctx.surface),
                        "vkCreateXlibSurfaceKHR");
    }
#endif
#if USE_WAYLAND
    case VID_NATIVE_WAYLAND: {
        VkWaylandSurfaceCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .display = (struct wl_display *)native->handle.wayland.display,
            .surface = (struct wl_surface *)native->handle.wayland.surface,
        };
        return VK_Check(vkCreateWaylandSurfaceKHR(vk_state.ctx.instance, &create_info, NULL,
                                                  &vk_state.ctx.surface),
                        "vkCreateWaylandSurfaceKHR");
    }
#endif
    case VID_NATIVE_SDL:
#if USE_SDL
        if (!SDL_Vulkan_CreateSurface((SDL_Window *)native->handle.sdl.window,
                                      vk_state.ctx.instance, NULL, &vk_state.ctx.surface)) {
            const char *error = SDL_GetError();
            Com_SetLastError(error && *error
                                 ? va("Vulkan: SDL surface creation failed: %s", error)
                                 : "Vulkan: SDL surface creation failed");
            return false;
        }
        return true;
#else
        Com_SetLastError("Vulkan: SDL native window handles are not supported in this build");
        return false;
#endif
    default:
        Com_SetLastError("Vulkan: unsupported video driver for surface creation");
        return false;
    }
}

static bool VK_DeviceHasExtension(VkPhysicalDevice device, const char *ext)
{
    uint32_t count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    if (result != VK_SUCCESS || count == 0) {
        return false;
    }

    VkExtensionProperties *props = VK_AllocArray(count, sizeof(*props),
                                                 "device extensions");
    if (!props) {
        return false;
    }

    result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, props);
    if (result != VK_SUCCESS) {
        Z_Free(props);
        return false;
    }

    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!strcmp(props[i].extensionName, ext)) {
            found = true;
            break;
        }
    }

    Z_Free(props);
    return found;
}

static bool VK_FindQueueFamily(VkPhysicalDevice device, uint32_t *out_index)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    if (!count)
        return false;

    VkQueueFamilyProperties *props = VK_AllocArray(count, sizeof(*props),
                                                   "queue families");
    if (!props) {
        return false;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;

        VkBool32 present_supported = VK_FALSE;
        VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, i,
                                                               vk_state.ctx.surface,
                                                               &present_supported);
        if (result != VK_SUCCESS) {
            Z_Free(props);
            return VK_Check(result, "vkGetPhysicalDeviceSurfaceSupportKHR");
        }
        if (present_supported) {
            *out_index = i;
            found = true;
            break;
        }
    }

    Z_Free(props);
    return found;
}

static bool VK_SelectPhysicalDevice(void)
{
    uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(vk_state.ctx.instance, &count, NULL);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkEnumeratePhysicalDevices");
    }
    if (count == 0) {
        Com_SetLastError("Vulkan: no physical devices reported");
        return false;
    }

    VkPhysicalDevice *devices = VK_AllocArray(count, sizeof(*devices),
                                              "physical devices");
    if (!devices) {
        return false;
    }

    result = vkEnumeratePhysicalDevices(vk_state.ctx.instance, &count, devices);
    if (result != VK_SUCCESS) {
        Z_Free(devices);
        return VK_Check(result, "vkEnumeratePhysicalDevices");
    }

    VkPhysicalDevice picked = VK_NULL_HANDLE;
    uint32_t queue_family = 0;

    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDevice device = devices[i];
        if (!VK_DeviceHasExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;

        uint32_t family = 0;
        if (!VK_FindQueueFamily(device, &family))
            continue;

        picked = device;
        queue_family = family;
        break;
    }

    if (picked == VK_NULL_HANDLE) {
        Z_Free(devices);
        Com_SetLastError("Vulkan: no compatible physical device found");
        return false;
    }

    vk_state.ctx.physical_device = picked;
    vk_state.ctx.graphics_queue_family = queue_family;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(picked, &props);
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(picked, &features);
    vk_state.ctx.sampler_anisotropy_supported =
        features.samplerAnisotropy == VK_TRUE;
    vk_state.ctx.max_sampler_anisotropy =
        vk_state.ctx.sampler_anisotropy_supported
            ? max(props.limits.maxSamplerAnisotropy, 1.0f) : 1.0f;
    Com_Printf("Vulkan device: %s\n", props.deviceName);

    Z_Free(devices);
    return true;
}

static bool VK_CreateDevice(void)
{
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk_state.ctx.graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char *extensions[2] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    uint32_t enabled_ext_count = 1;

    if (VK_DeviceHasExtension(vk_state.ctx.physical_device,
                              VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        extensions[enabled_ext_count++] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
    }

    VkPhysicalDeviceFeatures enabled_features = { 0 };
    if (vk_state.ctx.sampler_anisotropy_supported) {
        enabled_features.samplerAnisotropy = VK_TRUE;
    }

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = enabled_ext_count,
        .ppEnabledExtensionNames = extensions,
        .pEnabledFeatures = &enabled_features,
    };

    if (!VK_Check(vkCreateDevice(vk_state.ctx.physical_device, &create_info, NULL,
                                 &vk_state.ctx.device),
                  "vkCreateDevice")) {
        return false;
    }

    vkGetDeviceQueue(vk_state.ctx.device, vk_state.ctx.graphics_queue_family, 0,
                     &vk_state.ctx.graphics_queue);

    return true;
}

static bool VK_CreateCommandPool(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        Com_SetLastError("Vulkan: device unavailable for command pool creation");
        return false;
    }

    if (ctx->command_pool) {
        return true;
    }

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_queue_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };

    VkResult result = vkCreateCommandPool(ctx->device, &pool_info, NULL, &ctx->command_pool);
    return VK_Check(result, "vkCreateCommandPool");
}

static VkSurfaceFormatKHR VK_ChooseSurfaceFormat(const VkSurfaceFormatKHR *formats,
                                                 uint32_t count)
{
    if (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        VkSurfaceFormatKHR chosen = {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        };
        return chosen;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }

    // The native raster shaders currently produce legacy display-referred
    // code values. Prefer an UNORM target even on unusual surfaces; selecting
    // an SRGB image format here would apply a second transfer function.
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
            formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            return formats[i];
        }
    }

    Com_WPrintf("Vulkan: no compatible UNORM swapchain format; using format %d\n",
                (int)formats[0].format);

    return formats[0];
}

static VkPresentModeKHR VK_ChoosePresentMode(const VkPresentModeKHR *modes, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_FIFO_KHR)
            return modes[i];
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D VK_ChooseSwapExtent(const VkSurfaceCapabilitiesKHR *caps,
                                      uint32_t width, uint32_t height)
{
    if (caps->currentExtent.width != UINT32_MAX) {
        return caps->currentExtent;
    }

    VkExtent2D extent = {
        .width = VK_ClampU32(width, caps->minImageExtent.width, caps->maxImageExtent.width),
        .height = VK_ClampU32(height, caps->minImageExtent.height, caps->maxImageExtent.height),
    };

    return extent;
}

static void VK_DestroyLiquidSceneResources(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        return;
    }

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        VK_UI_DestroyExternalImageDescriptor(&frame->liquid_scene_descriptor_set);
        // VK_UI may already be shut down during final renderer teardown, in
        // which case its descriptor pool has destroyed the set on our behalf.
        frame->liquid_scene_descriptor_set = VK_NULL_HANDLE;
        if (frame->liquid_scene_view) {
            vkDestroyImageView(ctx->device, frame->liquid_scene_view, NULL);
            frame->liquid_scene_view = VK_NULL_HANDLE;
        }
        if (frame->liquid_scene_image) {
            vkDestroyImage(ctx->device, frame->liquid_scene_image, NULL);
            frame->liquid_scene_image = VK_NULL_HANDLE;
        }
        if (frame->liquid_scene_memory) {
            vkFreeMemory(ctx->device, frame->liquid_scene_memory, NULL);
            frame->liquid_scene_memory = VK_NULL_HANDLE;
        }
        frame->liquid_scene_initialized = false;
    }
}

static void VK_DestroyLinearSceneResources(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        return;
    }
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        if (frame->linear_scene_framebuffer) {
            vkDestroyFramebuffer(ctx->device, frame->linear_scene_framebuffer,
                                 NULL);
        }
        VK_UI_DestroyExternalImageDescriptor(
            &frame->linear_scene_copy_descriptor_set);
        VK_UI_DestroyExternalImageDescriptor(
            &frame->linear_scene_descriptor_set);
        VK_UI_DestroyExternalImageDescriptor(
            &frame->linear_scene_copy_base_descriptor_set);
        if (frame->linear_scene_copy_base_view) {
            vkDestroyImageView(ctx->device, frame->linear_scene_copy_base_view,
                               NULL);
        }
        if (frame->linear_scene_copy_view) {
            vkDestroyImageView(ctx->device, frame->linear_scene_copy_view, NULL);
        }
        if (frame->linear_scene_copy_image) {
            vkDestroyImage(ctx->device, frame->linear_scene_copy_image, NULL);
        }
        if (frame->linear_scene_copy_memory) {
            vkFreeMemory(ctx->device, frame->linear_scene_copy_memory, NULL);
        }
        if (frame->linear_scene_view) {
            vkDestroyImageView(ctx->device, frame->linear_scene_view, NULL);
        }
        if (frame->linear_scene_image) {
            vkDestroyImage(ctx->device, frame->linear_scene_image, NULL);
        }
        if (frame->linear_scene_memory) {
            vkFreeMemory(ctx->device, frame->linear_scene_memory, NULL);
        }
        frame->linear_scene_copy_descriptor_set = VK_NULL_HANDLE;
        frame->linear_scene_descriptor_set = VK_NULL_HANDLE;
        frame->linear_scene_copy_base_descriptor_set = VK_NULL_HANDLE;
        frame->linear_scene_framebuffer = VK_NULL_HANDLE;
        frame->linear_scene_copy_view = VK_NULL_HANDLE;
        frame->linear_scene_copy_base_view = VK_NULL_HANDLE;
        frame->linear_scene_copy_image = VK_NULL_HANDLE;
        frame->linear_scene_copy_memory = VK_NULL_HANDLE;
        frame->linear_scene_view = VK_NULL_HANDLE;
        frame->linear_scene_image = VK_NULL_HANDLE;
        frame->linear_scene_memory = VK_NULL_HANDLE;
        frame->linear_scene_copy_initialized = false;
        frame->linear_scene_copy_mips_initialized = false;
        frame->linear_scene_direct_sampled = false;
    }
}

static bool VK_CreateLinearSceneResources(vk_context_t *ctx)
{
    if (!ctx || !ctx->scene_format || !ctx->scene_extent.width ||
        !ctx->scene_extent.height || !ctx->frame_count) {
        return false;
    }
    const uint32_t copy_mip_levels = max(ctx->linear_scene_mip_levels, 1u);
    VkImageCreateInfo scene_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = ctx->scene_format,
        .extent = { ctx->scene_extent.width, ctx->scene_extent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImageCreateInfo copy_info = scene_info;
    copy_info.mipLevels = copy_mip_levels;
    copy_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        (copy_mip_levels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = ctx->scene_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        VkImage *images[] = { &frame->linear_scene_image,
                              &frame->linear_scene_copy_image };
        VkDeviceMemory *memories[] = { &frame->linear_scene_memory,
                                        &frame->linear_scene_copy_memory };
        const VkImageCreateInfo *infos[] = { &scene_info, &copy_info };
        for (uint32_t image = 0; image < q_countof(images); ++image) {
            result = vkCreateImage(ctx->device, infos[image], NULL, images[image]);
            if (result != VK_SUCCESS) goto unavailable;
            VkMemoryRequirements requirements;
            vkGetImageMemoryRequirements(ctx->device, *images[image], &requirements);
            uint32_t memory_type = VK_FindMemoryType(
                ctx->physical_device, requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (memory_type == UINT32_MAX) {
                result = VK_ERROR_FEATURE_NOT_PRESENT;
                goto unavailable;
            }
            VkMemoryAllocateInfo allocation = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = requirements.size,
                .memoryTypeIndex = memory_type,
            };
            result = vkAllocateMemory(ctx->device, &allocation, NULL, memories[image]);
            if (result != VK_SUCCESS) goto unavailable;
            result = vkBindImageMemory(ctx->device, *images[image], *memories[image], 0);
            if (result != VK_SUCCESS) goto unavailable;
        }
        view_info.image = frame->linear_scene_image;
        view_info.subresourceRange.levelCount = 1;
        result = vkCreateImageView(ctx->device, &view_info, NULL,
                                   &frame->linear_scene_view);
        if (result != VK_SUCCESS) goto unavailable;
        frame->linear_scene_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                frame->linear_scene_view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->linear_scene_descriptor_set) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto unavailable;
        }
        view_info.image = frame->linear_scene_copy_image;
        view_info.subresourceRange.levelCount = 1;
        result = vkCreateImageView(ctx->device, &view_info, NULL,
                                   &frame->linear_scene_copy_base_view);
        if (result != VK_SUCCESS) goto unavailable;
        frame->linear_scene_copy_base_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                frame->linear_scene_copy_base_view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->linear_scene_copy_base_descriptor_set) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto unavailable;
        }
        view_info.subresourceRange.levelCount = copy_mip_levels;
        result = vkCreateImageView(ctx->device, &view_info, NULL,
                                   &frame->linear_scene_copy_view);
        if (result != VK_SUCCESS) goto unavailable;
        frame->linear_scene_copy_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                frame->linear_scene_copy_view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->linear_scene_copy_descriptor_set) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto unavailable;
        }
    }
    return true;

unavailable:
    Com_WPrintf("Vulkan: native float scene resources unavailable (%s); "
                "retaining LDR scene path.\n", VK_ResultString(result));
    VK_DestroyLinearSceneResources(ctx);
    return false;
}

static bool VK_CreateLinearSceneFramebuffers(vk_context_t *ctx)
{
    if (!ctx || !ctx->device || !ctx->scene_render_pass ||
        !ctx->scene_extent.width || !ctx->scene_extent.height) {
        return false;
    }

    for (uint32_t frame_index = 0; frame_index < ctx->frame_count;
         ++frame_index) {
        vk_frame_context_t *frame = &ctx->frames[frame_index];
        if (!frame->linear_scene_view || !frame->depth_view) {
            Com_SetLastError("Vulkan: scaled scene framebuffer attachment unavailable");
            return false;
        }
        if (frame->linear_scene_framebuffer) {
            vkDestroyFramebuffer(ctx->device, frame->linear_scene_framebuffer,
                                 NULL);
            frame->linear_scene_framebuffer = VK_NULL_HANDLE;
        }
        VkImageView attachments[] = {
            frame->linear_scene_view,
            frame->depth_view,
        };
        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = ctx->scene_render_pass,
            .attachmentCount = q_countof(attachments),
            .pAttachments = attachments,
            .width = ctx->scene_extent.width,
            .height = ctx->scene_extent.height,
            .layers = 1,
        };
        VkResult result = vkCreateFramebuffer(
            ctx->device, &framebuffer_info, NULL,
            &frame->linear_scene_framebuffer);
        if (result != VK_SUCCESS) {
            return VK_Check(result, "vkCreateFramebuffer(linear scene)");
        }
    }
    return true;
}

// Extent-only scene refreshes keep these render passes (and their pipelines)
// alive because the attachment formats do not change. The image family below
// is the only bloom-emission state whose dimensions track scene_extent.
static void VK_DestroyBloomEmissionImages(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        return;
    }

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        if (frame->bloom_rim_emission_framebuffer) {
            vkDestroyFramebuffer(ctx->device,
                                 frame->bloom_rim_emission_framebuffer, NULL);
            frame->bloom_rim_emission_framebuffer = VK_NULL_HANDLE;
        }
        if (frame->bloom_emission_framebuffer) {
            vkDestroyFramebuffer(ctx->device, frame->bloom_emission_framebuffer, NULL);
            frame->bloom_emission_framebuffer = VK_NULL_HANDLE;
        }
        if (frame->bloom_emission_view) {
            vkDestroyImageView(ctx->device, frame->bloom_emission_view, NULL);
            frame->bloom_emission_view = VK_NULL_HANDLE;
        }
        if (frame->bloom_emission_image) {
            vkDestroyImage(ctx->device, frame->bloom_emission_image, NULL);
            frame->bloom_emission_image = VK_NULL_HANDLE;
        }
        if (frame->bloom_emission_memory) {
            vkFreeMemory(ctx->device, frame->bloom_emission_memory, NULL);
            frame->bloom_emission_memory = VK_NULL_HANDLE;
        }
        frame->bloom_emission_initialized = false;
    }
}

static void VK_DestroyBloomEmissionResources(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        return;
    }

    VK_DestroyBloomEmissionImages(ctx);
    if (ctx->bloom_extract_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->bloom_extract_render_pass, NULL);
        ctx->bloom_extract_render_pass = VK_NULL_HANDLE;
    }
    if (ctx->bloom_overlay_extract_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->bloom_overlay_extract_render_pass,
                            NULL);
        ctx->bloom_overlay_extract_render_pass = VK_NULL_HANDLE;
    }
    if (ctx->bloom_rim_extract_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->bloom_rim_extract_render_pass,
                            NULL);
        ctx->bloom_rim_extract_render_pass = VK_NULL_HANDLE;
    }
}

// This pass mirrors OpenGL's bloom MRT without adding a permanent second
// attachment to the normal scene render pass. It is recorded only while the
// native bloom effect is active and retains the existing scene depth.
static bool VK_CreateBloomEmissionResources(vk_context_t *ctx)
{
    if (!ctx || !ctx->device || !ctx->frame_count ||
        !ctx->scene_extent.width || !ctx->scene_extent.height) {
        return false;
    }

    const VkFormat bloom_format = ctx->frames[0].linear_scene_image
        ? ctx->scene_format : ctx->swapchain.format;
    VkResult result;

    // Render-pass compatibility is defined by formats and subpasses, not
    // framebuffer extent. Retain these passes on a scaled-scene resize so
    // world/entity bloom pipelines remain valid and avoid needless pipeline
    // cache churn.
    if (!ctx->bloom_extract_render_pass) {
      VkAttachmentDescription attachments[2] = {
          {
              .format = bloom_format,
              .samples = VK_SAMPLE_COUNT_1_BIT,
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          },
          {
              .format = ctx->swapchain.depth_format,
              .samples = VK_SAMPLE_COUNT_1_BIT,
              .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
              .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          },
      };
      VkAttachmentReference color_ref = {
          .attachment = 0,
          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      };
      VkAttachmentReference depth_ref = {
          .attachment = 1,
          .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      };
      VkSubpassDescription subpass = {
          .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
          .colorAttachmentCount = 1,
          .pColorAttachments = &color_ref,
          .pDepthStencilAttachment = &depth_ref,
      };
      VkSubpassDependency dependencies[2] = {
          {
              .srcSubpass = VK_SUBPASS_EXTERNAL,
              .dstSubpass = 0,
              .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
              .srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
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
          .attachmentCount = q_countof(attachments),
          .pAttachments = attachments,
          .subpassCount = 1,
          .pSubpasses = &subpass,
          .dependencyCount = q_countof(dependencies),
          .pDependencies = dependencies,
      };
      result = vkCreateRenderPass(ctx->device, &render_pass_info, NULL,
                                  &ctx->bloom_extract_render_pass);
      if (result != VK_SUCCESS) {
        Com_WPrintf("Vulkan: bloom emission render pass unavailable (%s).\n",
                    VK_ResultString(result));
        return false;
      }

      // Front/depth-hack entities are submitted after the liquid scene has
      // been copied. Preserve already extracted world emission and replay only
      // their authored sources in a small load pass after that late entity
      // pass.
      VkAttachmentDescription overlay_attachments[2] = {
          attachments[0],
          attachments[1],
      };
      overlay_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      VkRenderPassCreateInfo overlay_render_pass_info = render_pass_info;
      overlay_render_pass_info.pAttachments = overlay_attachments;
      result = vkCreateRenderPass(ctx->device, &overlay_render_pass_info, NULL,
                                  &ctx->bloom_overlay_extract_render_pass);
      if (result != VK_SUCCESS) {
        Com_WPrintf("Vulkan: late depth-hack bloom pass unavailable (%s).\n",
                    VK_ResultString(result));
        ctx->bloom_overlay_extract_render_pass = VK_NULL_HANDLE;
      }

      // Only an active rim receiver needs sampled depth. Keep the established
      // clear-and-depth-test extract pass for world, glowmap, and shell
      // sources; this second pass loads its emission colour and samples
      // read-only depth solely to resolve equal-depth alias replay without
      // bloom leaking through an occluder.
      if (vk_dof_supported) {
        VkAttachmentDescription rim_attachments[2] = {
            attachments[0],
            attachments[1],
        };
        rim_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        rim_attachments[0].initialLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        rim_attachments[1].initialLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        rim_attachments[1].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference rim_depth_ref = {
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        };
        VkSubpassDescription rim_subpass = subpass;
        rim_subpass.pDepthStencilAttachment = &rim_depth_ref;
        VkSubpassDependency rim_dependencies[2] = {
            dependencies[0],
            dependencies[1],
        };
        rim_dependencies[0].dstStageMask |=
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        rim_dependencies[0].dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo rim_render_pass_info = render_pass_info;
        rim_render_pass_info.pAttachments = rim_attachments;
        rim_render_pass_info.pSubpasses = &rim_subpass;
        rim_render_pass_info.pDependencies = rim_dependencies;
        result = vkCreateRenderPass(ctx->device, &rim_render_pass_info, NULL,
                                    &ctx->bloom_rim_extract_render_pass);
        if (result != VK_SUCCESS) {
          Com_WPrintf(
              "Vulkan: sampled-depth rim bloom pass unavailable (%s).\n",
              VK_ResultString(result));
          ctx->bloom_rim_extract_render_pass = VK_NULL_HANDLE;
        }
      }
    }

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = bloom_format,
        .extent = {
            .width = ctx->scene_extent.width,
            .height = ctx->scene_extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = bloom_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        result = vkCreateImage(ctx->device, &image_info, NULL,
                               &frame->bloom_emission_image);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(ctx->device, frame->bloom_emission_image,
                                     &requirements);
        uint32_t memory_type = VK_FindMemoryType(
            ctx->physical_device, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memory_type == UINT32_MAX) {
            result = VK_ERROR_FEATURE_NOT_PRESENT;
            goto unavailable;
        }
        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type,
        };
        result = vkAllocateMemory(ctx->device, &alloc_info, NULL,
                                  &frame->bloom_emission_memory);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        result = vkBindImageMemory(ctx->device, frame->bloom_emission_image,
                                   frame->bloom_emission_memory, 0);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        view_info.image = frame->bloom_emission_image;
        result = vkCreateImageView(ctx->device, &view_info, NULL,
                                   &frame->bloom_emission_view);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        VkImageView framebuffer_attachments[] = {
            frame->bloom_emission_view,
            frame->depth_view,
        };
        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = ctx->bloom_extract_render_pass,
            .attachmentCount = q_countof(framebuffer_attachments),
            .pAttachments = framebuffer_attachments,
            .width = ctx->scene_extent.width,
            .height = ctx->scene_extent.height,
            .layers = 1,
        };
        result = vkCreateFramebuffer(ctx->device, &framebuffer_info, NULL,
                                     &frame->bloom_emission_framebuffer);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        if (ctx->bloom_rim_extract_render_pass) {
            framebuffer_info.renderPass = ctx->bloom_rim_extract_render_pass;
            result = vkCreateFramebuffer(ctx->device, &framebuffer_info, NULL,
                                         &frame->bloom_rim_emission_framebuffer);
            if (result != VK_SUCCESS) {
                Com_WPrintf("Vulkan: sampled-depth rim bloom framebuffer unavailable (%s).\n",
                            VK_ResultString(result));
                ctx->bloom_rim_extract_render_pass = VK_NULL_HANDLE;
            }
        }
    }
    return true;

unavailable:
    Com_WPrintf("Vulkan: bloom emission image unavailable (%s); using scene fallback.\n",
                VK_ResultString(result));
    VK_DestroyBloomEmissionImages(ctx);
    return false;
}

static void VK_CreateLiquidSceneResources(vk_context_t *ctx,
                                          bool swapchain_supports_transfer_src,
                                          VkFormatFeatureFlags format_features)
{
    if (!ctx || !ctx->device || !ctx->swapchain.handle ||
        !swapchain_supports_transfer_src ||
        !(format_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        return;
    }

    VkResult result = VK_SUCCESS;
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = ctx->swapchain.format,
        .extent = {
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = ctx->swapchain.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        result = vkCreateImage(ctx->device, &image_info, NULL,
                               &frame->liquid_scene_image);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(ctx->device, frame->liquid_scene_image,
                                     &requirements);
        uint32_t memory_type = VK_FindMemoryType(
            ctx->physical_device, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memory_type == UINT32_MAX) {
            result = VK_ERROR_FEATURE_NOT_PRESENT;
            goto unavailable;
        }
        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type,
        };
        result = vkAllocateMemory(ctx->device, &alloc_info, NULL,
                                  &frame->liquid_scene_memory);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        result = vkBindImageMemory(ctx->device, frame->liquid_scene_image,
                                   frame->liquid_scene_memory, 0);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }

        view_info.image = frame->liquid_scene_image;
        result = vkCreateImageView(ctx->device, &view_info, NULL,
                                   &frame->liquid_scene_view);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        frame->liquid_scene_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                frame->liquid_scene_view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->liquid_scene_descriptor_set) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto unavailable;
        }
        frame->liquid_scene_initialized = false;
    }
    return;

unavailable:
    Com_WPrintf("Vulkan: native liquid scene copy unavailable (%s); refraction disabled.\n",
                VK_ResultString(result));
    VK_DestroyLiquidSceneResources(ctx);
}

static void VK_DestroySwapchain(vk_context_t *ctx)
{
    if (!ctx->device)
        return;

    VK_PostProcess_DestroySwapchainResources(ctx);
    VK_World_DestroySwapchainResources(ctx);
    VK_Entity_DestroySwapchainResources(ctx);
    VK_Debug_DestroySwapchainResources(ctx);
    VK_UI_DestroySwapchainResources(ctx);
    VK_DestroyBloomEmissionResources(ctx);
    VK_DestroyLinearSceneResources(ctx);

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        if (frame->framebuffers) {
            for (uint32_t image = 0; image < ctx->swapchain.image_count; ++image) {
                if (frame->framebuffers[image]) {
                    vkDestroyFramebuffer(ctx->device, frame->framebuffers[image], NULL);
                }
            }
            Z_Free(frame->framebuffers);
            frame->framebuffers = NULL;
        }
        VK_UI_DestroyExternalImageDescriptor(
            &frame->bloom_depth_descriptor_set);
        // VK_UI can already be shut down during final renderer teardown.
        frame->bloom_depth_descriptor_set = VK_NULL_HANDLE;
        if (frame->depth_sample_view) {
            vkDestroyImageView(ctx->device, frame->depth_sample_view, NULL);
            frame->depth_sample_view = VK_NULL_HANDLE;
        }
        if (frame->depth_view) {
            vkDestroyImageView(ctx->device, frame->depth_view, NULL);
            frame->depth_view = VK_NULL_HANDLE;
        }
        if (frame->depth_image) {
            vkDestroyImage(ctx->device, frame->depth_image, NULL);
            frame->depth_image = VK_NULL_HANDLE;
        }
        if (frame->depth_memory) {
            vkFreeMemory(ctx->device, frame->depth_memory, NULL);
            frame->depth_memory = VK_NULL_HANDLE;
        }
        if (frame->in_flight_fence) {
            vkDestroyFence(ctx->device, frame->in_flight_fence, NULL);
            frame->in_flight_fence = VK_NULL_HANDLE;
        }
        if (frame->image_available) {
            vkDestroySemaphore(ctx->device, frame->image_available, NULL);
            frame->image_available = VK_NULL_HANDLE;
        }
        if (ctx->command_pool && frame->command_buffer) {
            vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1,
                                 &frame->command_buffer);
            frame->command_buffer = VK_NULL_HANDLE;
        }
        frame->submitted = false;
    }

    if (ctx->swapchain.render_finished) {
        for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
            if (ctx->swapchain.render_finished[i]) {
                vkDestroySemaphore(ctx->device,
                                   ctx->swapchain.render_finished[i], NULL);
            }
        }
        Z_Free(ctx->swapchain.render_finished);
        ctx->swapchain.render_finished = NULL;
    }

    vk_dof_supported = false;

    VK_DestroyLiquidSceneResources(ctx);

    if (ctx->scene_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->scene_render_pass, NULL);
        ctx->scene_render_pass = VK_NULL_HANDLE;
    }
    if (ctx->scene_load_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->scene_load_render_pass, NULL);
        ctx->scene_load_render_pass = VK_NULL_HANDLE;
    }
    if (ctx->presentation_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->presentation_render_pass, NULL);
        ctx->presentation_render_pass = VK_NULL_HANDLE;
    }
    if (ctx->presentation_overlay_render_pass) {
        vkDestroyRenderPass(ctx->device,
                            ctx->presentation_overlay_render_pass, NULL);
        ctx->presentation_overlay_render_pass = VK_NULL_HANDLE;
    }
    if (ctx->presentation_load_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->presentation_load_render_pass,
                            NULL);
        ctx->presentation_load_render_pass = VK_NULL_HANDLE;
    }

    if (ctx->swapchain.views) {
        for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
            if (ctx->swapchain.views[i]) {
                vkDestroyImageView(ctx->device, ctx->swapchain.views[i], NULL);
            }
        }
        Z_Free(ctx->swapchain.views);
        ctx->swapchain.views = NULL;
    }

    if (ctx->swapchain.images) {
        Z_Free(ctx->swapchain.images);
        ctx->swapchain.images = NULL;
    }

    if (ctx->swapchain.handle) {
        vkDestroySwapchainKHR(ctx->device, ctx->swapchain.handle, NULL);
        ctx->swapchain.handle = VK_NULL_HANDLE;
    }

    if (ctx->swapchain.image_frame_slots) {
        Z_Free(ctx->swapchain.image_frame_slots);
        ctx->swapchain.image_frame_slots = NULL;
    }
    if (ctx->swapchain.image_presented) {
        Z_Free(ctx->swapchain.image_presented);
        ctx->swapchain.image_presented = NULL;
    }

    ctx->frame_count = 0;
    ctx->current_frame = 0;
    ctx->swapchain.image_count = 0;
    ctx->swapchain.format = VK_FORMAT_UNDEFINED;
    ctx->swapchain.depth_format = VK_FORMAT_UNDEFINED;
    ctx->scene_format = VK_FORMAT_UNDEFINED;
    memset(&ctx->scene_extent, 0, sizeof(ctx->scene_extent));
    ctx->scene_is_float = false;
    ctx->scene_offscreen_supported = false;
    ctx->scaled_scene_blit_supported = false;
    ctx->linear_scene_format = VK_FORMAT_UNDEFINED;
    ctx->linear_scene_supported = false;
    ctx->linear_scene_mips_supported = false;
    ctx->linear_scene_mip_levels = 0;
    memset(&ctx->swapchain.extent, 0, sizeof(ctx->swapchain.extent));
}

static bool VK_CreateSwapchain(uint32_t width, uint32_t height)
{
    vk_context_t *ctx = &vk_state.ctx;

    VkSurfaceCapabilitiesKHR caps;
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device,
                                                                ctx->surface, &caps);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    }

    uint32_t format_count = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface,
                                                  &format_count, NULL);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    }
    if (!format_count) {
        Com_SetLastError("Vulkan: no surface formats reported");
        return false;
    }

    VkSurfaceFormatKHR *formats = VK_AllocArray(format_count, sizeof(*formats),
                                                "surface formats");
    if (!formats) {
        return false;
    }

    result = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface,
                                                  &format_count, formats);
    if (result != VK_SUCCESS) {
        Z_Free(formats);
        return VK_Check(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    }
    if (!format_count) {
        Z_Free(formats);
        Com_SetLastError("Vulkan: no surface formats reported");
        return false;
    }

    VkSurfaceFormatKHR chosen_format = VK_ChooseSurfaceFormat(formats, format_count);
    Z_Free(formats);

    uint32_t present_count = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface,
                                                       &present_count, NULL);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
    if (!present_count) {
        Com_SetLastError("Vulkan: no present modes reported");
        return false;
    }

    VkPresentModeKHR *present_modes = VK_AllocArray(present_count, sizeof(*present_modes),
                                                    "present modes");
    if (!present_modes) {
        return false;
    }

    result = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface,
                                                       &present_count, present_modes);
    if (result != VK_SUCCESS) {
        Z_Free(present_modes);
        return VK_Check(result, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
    if (!present_count) {
        Z_Free(present_modes);
        Com_SetLastError("Vulkan: no present modes reported");
        return false;
    }

    VkPresentModeKHR present_mode = VK_ChoosePresentMode(present_modes, present_count);
    Z_Free(present_modes);

    VkExtent2D extent = VK_ChooseSwapExtent(&caps, width, height);
    if (!extent.width || !extent.height) {
        Com_SetLastError("Vulkan: swapchain extent is zero");
        return false;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    vk_screenshot_supported =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (vk_screenshot_supported) {
        image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    const bool swapchain_supports_transfer_dst =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;
    if (swapchain_supports_transfer_dst) {
        image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VkFormatProperties scene_format_properties;
    vkGetPhysicalDeviceFormatProperties(ctx->physical_device, chosen_format.format,
                                        &scene_format_properties);
    VkFormatProperties linear_scene_format_properties;
    vkGetPhysicalDeviceFormatProperties(ctx->physical_device,
                                        VK_FORMAT_R16G16B16A16_SFLOAT,
                                        &linear_scene_format_properties);
    const VkFormatFeatureFlags linear_scene_required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    ctx->scene_offscreen_supported =
        (scene_format_properties.optimalTilingFeatures &
         linear_scene_required) == linear_scene_required;
    const VkFormatFeatureFlags scaled_scene_blit_required =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    ctx->scaled_scene_blit_supported = swapchain_supports_transfer_dst &&
        (scene_format_properties.optimalTilingFeatures &
         scaled_scene_blit_required) == scaled_scene_blit_required;
    ctx->linear_scene_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ctx->linear_scene_supported =
        (linear_scene_format_properties.optimalTilingFeatures &
         linear_scene_required) == linear_scene_required;
    const VkFormatFeatureFlags linear_scene_mip_required =
        linear_scene_required | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    ctx->linear_scene_mips_supported =
        (linear_scene_format_properties.optimalTilingFeatures &
         linear_scene_mip_required) == linear_scene_mip_required;
    ctx->linear_scene_mip_levels = 1;
    if (!ctx->linear_scene_supported) {
        Com_WPrintf("Vulkan: R16G16B16A16 float scene target unsupported; "
                    "HDR scene path remains disabled.\n");
    }
    const bool swapchain_supports_transfer_src =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx->surface,
        .minImageCount = image_count,
        .imageFormat = chosen_format.format,
        .imageColorSpace = chosen_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = image_usage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    result = vkCreateSwapchainKHR(ctx->device, &create_info, NULL, &ctx->swapchain.handle);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkCreateSwapchainKHR");
    }

    result = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain.handle, &image_count, NULL);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkGetSwapchainImagesKHR");
    }
    if (!image_count) {
        Com_SetLastError("Vulkan: no swapchain images reported");
        VK_DestroySwapchain(ctx);
        return false;
    }

    ctx->swapchain.images = VK_AllocArray(image_count, sizeof(*ctx->swapchain.images),
                                          "swapchain images");
    if (!ctx->swapchain.images) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    result = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain.handle, &image_count,
                                     ctx->swapchain.images);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkGetSwapchainImagesKHR");
    }
    if (!image_count) {
        Com_SetLastError("Vulkan: no swapchain images reported");
        VK_DestroySwapchain(ctx);
        return false;
    }

    ctx->swapchain.image_count = image_count;
    ctx->swapchain.format = chosen_format.format;
    ctx->swapchain.extent = extent;
    ctx->frame_count = min((uint32_t)VK_MAX_FRAMES_IN_FLIGHT, image_count);
    if (!ctx->frame_count) {
        Com_SetLastError("Vulkan: swapchain has no usable frame contexts");
        VK_DestroySwapchain(ctx);
        return false;
    }

    cvar_t *vk_hdr_startup = Cvar_Get("vk_hdr", "0",
                                      CVAR_ARCHIVE | CVAR_RENDERER);
    cvar_t *vk_hdr_auto_startup = Cvar_Get("vk_hdr_auto_exposure", "0",
                                           CVAR_ARCHIVE | CVAR_RENDERER);
    ctx->scene_extent = extent;
    ctx->scene_format = chosen_format.format;
    ctx->scene_is_float = false;
    const VkExtent2D scaled_extent = VK_ResolutionScaleExtent(extent);
    const bool scale_requested = scaled_extent.width != extent.width ||
        scaled_extent.height != extent.height;
    if (vk_hdr_startup && vk_hdr_startup->integer &&
        ctx->linear_scene_supported) {
        ctx->scene_format = ctx->linear_scene_format;
        ctx->scene_is_float = true;
        ctx->scene_offscreen_supported = true;
    }
    if (scale_requested && !ctx->scene_is_float) {
        if (ctx->scene_offscreen_supported) {
            ctx->scene_extent = scaled_extent;
        } else {
            Com_WPrintf("Vulkan: swapchain format cannot support sampled "
                        "offscreen resolution scaling; rendering at native resolution.\n");
        }
    } else if (ctx->scene_is_float) {
        ctx->scene_extent = scaled_extent;
    }
    const bool offscreen_scene = ctx->scene_is_float ||
        ctx->scene_extent.width != extent.width ||
        ctx->scene_extent.height != extent.height;
    if (ctx->scene_is_float && vk_hdr_auto_startup &&
        vk_hdr_auto_startup->integer && ctx->linear_scene_mips_supported) {
        uint32_t max_dimension = max(ctx->scene_extent.width,
                                     ctx->scene_extent.height);
        while (max_dimension > 1) {
            ctx->linear_scene_mip_levels++;
            max_dimension >>= 1;
        }
    } else if (ctx->scene_is_float && vk_hdr_auto_startup &&
               vk_hdr_auto_startup->integer) {
        Com_WPrintf("Vulkan: HDR auto exposure requires float scene blit support; "
                    "using static exposure.\n");
    }
    if (offscreen_scene && !VK_CreateLinearSceneResources(ctx)) {
        Com_WPrintf("Vulkan: offscreen scene allocation failed; rendering at native resolution.\n");
        ctx->scene_extent = extent;
        ctx->scene_format = chosen_format.format;
        ctx->scene_is_float = false;
        ctx->scene_offscreen_supported = false;
    }
    if (!ctx->frames[0].linear_scene_image) {
        VK_CreateLiquidSceneResources(
            ctx, swapchain_supports_transfer_src,
            scene_format_properties.optimalTilingFeatures);
    }

    ctx->swapchain.views = VK_AllocArray(image_count, sizeof(*ctx->swapchain.views),
                                         "swapchain image views");
    if (!ctx->swapchain.views) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx->swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx->swapchain.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        result = vkCreateImageView(ctx->device, &view_info, NULL, &ctx->swapchain.views[i]);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateImageView");
        }
    }

    ctx->swapchain.depth_format = VK_ChooseDepthFormat(ctx->physical_device);
    if (ctx->swapchain.depth_format == VK_FORMAT_UNDEFINED) {
        Com_SetLastError("Vulkan: no supported depth format found");
        VK_DestroySwapchain(ctx);
        return false;
    }
    vk_dof_supported = VK_DepthFormatSupportsSampling(
        ctx->physical_device, ctx->swapchain.depth_format);
    if (!vk_dof_supported) {
        Com_WPrintf("Vulkan: depth format is not sampleable; depth-aware DOF disabled.\n");
    }

    VkImageCreateInfo depth_image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = ctx->swapchain.depth_format,
        .extent = {
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 (vk_dof_supported ? VK_IMAGE_USAGE_SAMPLED_BIT : 0),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (ctx->swapchain.depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        ctx->swapchain.depth_format == VK_FORMAT_D24_UNORM_S8_UINT) {
        depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = ctx->swapchain.depth_format,
        .subresourceRange = {
            .aspectMask = depth_aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        result = vkCreateImage(ctx->device, &depth_image_info, NULL,
                               &frame->depth_image);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateImage(depth)");
        }

        VkMemoryRequirements depth_requirements;
        vkGetImageMemoryRequirements(ctx->device, frame->depth_image,
                                     &depth_requirements);
        uint32_t depth_memory_type = VK_FindMemoryType(
            ctx->physical_device, depth_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (depth_memory_type == UINT32_MAX) {
            Com_SetLastError("Vulkan: no suitable depth memory type found");
            VK_DestroySwapchain(ctx);
            return false;
        }

        VkMemoryAllocateInfo depth_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = depth_requirements.size,
            .memoryTypeIndex = depth_memory_type,
        };
        result = vkAllocateMemory(ctx->device, &depth_alloc_info, NULL,
                                  &frame->depth_memory);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkAllocateMemory(depth)");
        }
        result = vkBindImageMemory(ctx->device, frame->depth_image,
                                   frame->depth_memory, 0);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkBindImageMemory(depth)");
        }

        depth_view_info.image = frame->depth_image;
        result = vkCreateImageView(ctx->device, &depth_view_info, NULL,
                                   &frame->depth_view);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateImageView(depth)");
        }
        if (vk_dof_supported) {
            VkImageViewCreateInfo sample_view_info = depth_view_info;
            sample_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            result = vkCreateImageView(ctx->device, &sample_view_info, NULL,
                                       &frame->depth_sample_view);
            if (result != VK_SUCCESS) {
                Com_WPrintf("Vulkan: depth sample view unavailable (%s); depth-aware DOF disabled.\n",
                            VK_ResultString(result));
                vk_dof_supported = false;
            } else {
                frame->bloom_depth_descriptor_set =
                    VK_UI_CreateExternalImageTripleDescriptor(
                        frame->depth_sample_view,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        frame->depth_sample_view,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        frame->depth_sample_view,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
                if (!frame->bloom_depth_descriptor_set) {
                    Com_WPrintf("Vulkan: rim bloom depth descriptor unavailable; using depth fallback.\n");
                }
            }
        }
    }

    const bool linear_scene =
        ctx->frames[0].linear_scene_image != VK_NULL_HANDLE;
    VkAttachmentDescription scene_attachments[2] = {
        {
            .format = linear_scene ? ctx->scene_format : ctx->swapchain.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = linear_scene
                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        {
            .format = ctx->swapchain.depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            // The native liquid phase reuses opaque depth after its scene
            // copy, so preserve it even on frames without transparent water.
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_DepthFormatHasStencil(ctx->swapchain.depth_format)
                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depth_ref = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };

    VkSubpassDependency dependencies[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
        },
    };

    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = q_countof(scene_attachments),
        .pAttachments = scene_attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = q_countof(dependencies),
        .pDependencies = dependencies,
    };

    result = vkCreateRenderPass(ctx->device, &render_pass_info, NULL,
                                &ctx->scene_render_pass);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkCreateRenderPass(scene)");
    }
    VkAttachmentDescription presentation_attachments[2] = {
        scene_attachments[0], scene_attachments[1],
    };
    presentation_attachments[0].format = ctx->swapchain.format;
    presentation_attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    render_pass_info.pAttachments = presentation_attachments;
    result = vkCreateRenderPass(ctx->device, &render_pass_info, NULL,
                                &ctx->presentation_render_pass);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkCreateRenderPass(presentation)");
    }

    // A compatible load pass lets native no-world entity previews overlay
    // the completed RmlUi shell without clearing its color attachment. Depth
    // is cleared because the menu preview is an independent scene.
    VkAttachmentDescription overlay_attachments[2] = {
        presentation_attachments[0],
        presentation_attachments[1],
    };
    overlay_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    overlay_attachments[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    overlay_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    overlay_attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkRenderPassCreateInfo overlay_render_pass_info = render_pass_info;
    overlay_render_pass_info.pAttachments = overlay_attachments;
    result = vkCreateRenderPass(ctx->device, &overlay_render_pass_info, NULL,
                                &ctx->presentation_overlay_render_pass);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkCreateRenderPass(preview overlay)");
    }

    // Transparent warp surfaces run after the opaque scene has been copied to
    // a sampled image. This pass loads both color and depth, retaining correct
    // depth rejection without feedback-looping the swapchain image.
    VkAttachmentDescription liquid_attachments[2] = {
        scene_attachments[0],
        scene_attachments[1],
    };
    liquid_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    liquid_attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    liquid_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    liquid_attachments[1].initialLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkRenderPassCreateInfo liquid_render_pass_info = render_pass_info;
    liquid_render_pass_info.pAttachments = liquid_attachments;
    // Keep this pass compatible with the base scene pipelines. Explicit image
    // barriers bracket the scene copy and the preserved depth attachment.
    liquid_render_pass_info.pDependencies = dependencies;
    result = vkCreateRenderPass(ctx->device, &liquid_render_pass_info, NULL,
                                &ctx->scene_load_render_pass);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkCreateRenderPass(scene load)");
    }
    VkAttachmentDescription presentation_load_attachments[2] = {
        presentation_attachments[0], presentation_attachments[1],
    };
    presentation_load_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    presentation_load_attachments[0].initialLayout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentation_load_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    presentation_load_attachments[1].initialLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    liquid_render_pass_info.pAttachments = presentation_load_attachments;
    result = vkCreateRenderPass(ctx->device, &liquid_render_pass_info, NULL,
                                &ctx->presentation_load_render_pass);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkCreateRenderPass(presentation load)");
    }

    // Optional accelerated bloom source. Failure is non-fatal: the
    // post-process module retains its scene-only fallback on constrained
    // implementations.
    VK_CreateBloomEmissionResources(ctx);

    if (linear_scene && !VK_CreateLinearSceneFramebuffers(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    for (uint32_t frame_index = 0; frame_index < ctx->frame_count; ++frame_index) {
        vk_frame_context_t *frame = &ctx->frames[frame_index];
        frame->framebuffers = VK_AllocArray(image_count,
                                            sizeof(*frame->framebuffers),
                                            "frame-context swapchain framebuffers");
        if (!frame->framebuffers) {
            VK_DestroySwapchain(ctx);
            return false;
        }
        for (uint32_t image_index = 0; image_index < image_count; ++image_index) {
            VkImageView framebuffer_attachments[] = {
                ctx->swapchain.views[image_index],
                frame->depth_view,
            };
            VkFramebufferCreateInfo framebuffer_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = ctx->presentation_render_pass,
                .attachmentCount = q_countof(framebuffer_attachments),
                .pAttachments = framebuffer_attachments,
                .width = ctx->swapchain.extent.width,
                .height = ctx->swapchain.extent.height,
                .layers = 1,
            };
            result = vkCreateFramebuffer(ctx->device, &framebuffer_info, NULL,
                                         &frame->framebuffers[image_index]);
            if (result != VK_SUCCESS) {
                VK_DestroySwapchain(ctx);
                return VK_Check(result, "vkCreateFramebuffer");
            }

        }
    }

    if (!ctx->command_pool) {
        Com_SetLastError("Vulkan: command pool unavailable during swapchain creation");
        VK_DestroySwapchain(ctx);
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = ctx->frame_count,
    };

    VkCommandBuffer command_buffers[VK_MAX_FRAMES_IN_FLIGHT] = { VK_NULL_HANDLE };
    result = vkAllocateCommandBuffers(ctx->device, &alloc_info, command_buffers);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkAllocateCommandBuffers");
    }
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        ctx->frames[i].command_buffer = command_buffers[i];
    }

    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    // A presentation wait may outlive the queue-submission fence. Associate
    // the render-finished semaphore with the acquired swapchain image so it is
    // not reused until presentation has released that image.
    ctx->swapchain.render_finished = VK_AllocArray(
        image_count, sizeof(*ctx->swapchain.render_finished),
        "render-finished semaphores");
    if (!ctx->swapchain.render_finished) {
        VK_DestroySwapchain(ctx);
        return false;
    }
    for (uint32_t i = 0; i < image_count; ++i) {
        result = vkCreateSemaphore(ctx->device, &sem_info, NULL,
                                   &ctx->swapchain.render_finished[i]);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateSemaphore(render finished)");
        }
    }

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        result = vkCreateSemaphore(ctx->device, &sem_info, NULL,
                                   &frame->image_available);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateSemaphore(image available)");
        }
        result = vkCreateFence(ctx->device, &fence_info, NULL,
                               &frame->in_flight_fence);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateFence");
        }
    }

    ctx->swapchain.image_frame_slots = VK_AllocArray(
        image_count, sizeof(*ctx->swapchain.image_frame_slots),
        "swapchain image frame ownership");
    ctx->swapchain.image_presented = VK_AllocArray(
        image_count, sizeof(*ctx->swapchain.image_presented),
        "swapchain image presentation state");
    if (!ctx->swapchain.image_frame_slots || !ctx->swapchain.image_presented) {
        VK_DestroySwapchain(ctx);
        return false;
    }
    for (uint32_t i = 0; i < image_count; ++i) {
        ctx->swapchain.image_frame_slots[i] = VK_INVALID_FRAME_SLOT;
        ctx->swapchain.image_presented[i] = false;
    }
    ctx->current_frame = 0;
    return true;
}

static bool VK_RecreateSwapchain(uint32_t width, uint32_t height)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (!ctx->device)
        return false;

    vkDeviceWaitIdle(ctx->device);
    VK_PostProcess_DestroySwapchainResources(ctx);
    VK_DestroySwapchain(ctx);

    if (!VK_CreateSwapchain(width, height)) {
        return false;
    }

    if (!VK_World_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    if (!VK_Entity_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    if (!VK_Debug_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    if (!VK_UI_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    if (!VK_PostProcess_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    VK_Debug_UpdateCapabilities(
        vk_screenshot_supported,
        VK_DepthFormatHasStencil(ctx->swapchain.depth_format),
        vk_dof_supported);

    vk_state.swapchain_dirty = false;
    return true;
}

static bool VK_WaitForSubmittedFrames(vk_context_t *ctx, const char *what);

static void VK_Screenshot_DestroyBuffer(void)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (vk_screenshot_buffer) {
        vkDestroyBuffer(ctx->device, vk_screenshot_buffer, NULL);
        vk_screenshot_buffer = VK_NULL_HANDLE;
    }
    if (vk_screenshot_memory) {
        vkFreeMemory(ctx->device, vk_screenshot_memory, NULL);
        vk_screenshot_memory = VK_NULL_HANDLE;
    }
    vk_screenshot_capacity = 0;
}

static bool VK_Screenshot_EnsureBuffer(VkDeviceSize size)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (!size) {
        Com_SetLastError("Vulkan: zero-sized screenshot readback buffer");
        return false;
    }

    if (vk_screenshot_buffer && vk_screenshot_capacity >= size) {
        return true;
    }

    VK_Screenshot_DestroyBuffer();

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_Check(vkCreateBuffer(ctx->device, &buffer_info, NULL,
                                 &vk_screenshot_buffer),
                  "vkCreateBuffer(screenshot)")) {
        return false;
    }

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(ctx->device, vk_screenshot_buffer, &reqs);

    uint32_t memory_type = VK_FindMemoryType(ctx->physical_device,
                                             reqs.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        Com_EPrintf("Vulkan screenshot: no host-visible memory type\n");
        VK_Screenshot_DestroyBuffer();
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = reqs.size,
        .memoryTypeIndex = memory_type,
    };
    if (!VK_Check(vkAllocateMemory(ctx->device, &alloc_info, NULL,
                                   &vk_screenshot_memory),
                  "vkAllocateMemory(screenshot)")) {
        VK_Screenshot_DestroyBuffer();
        return false;
    }
    if (!VK_Check(vkBindBufferMemory(ctx->device, vk_screenshot_buffer,
                                     vk_screenshot_memory, 0),
                  "vkBindBufferMemory(screenshot)")) {
        VK_Screenshot_DestroyBuffer();
        return false;
    }

    vk_screenshot_capacity = size;
    return true;
}

static void VK_Screenshot_RecordCopy(VkCommandBuffer cmd, uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    VkImage image = ctx->swapchain.images[image_index];

    VkImageMemoryBarrier to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_transfer);

    VkBufferImageCopy region = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = {
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .depth = 1,
        },
    };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vk_screenshot_buffer, 1, &region);

    VkBufferMemoryBarrier host_read = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = vk_screenshot_buffer,
        .size = VK_WHOLE_SIZE,
    };
    VkImageMemoryBarrier to_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT |
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, NULL, 1, &host_read, 1, &to_present);
}

static void VK_Screenshot_Complete(void)
{
    vk_context_t *ctx = &vk_state.ctx;

    vk_screenshot_armed = false;
    vk_screenshot_pending = false;

    if (!VK_WaitForSubmittedFrames(ctx, "vkWaitForFences(screenshot)")) {
        return;
    }

    uint32_t width = ctx->swapchain.extent.width;
    uint32_t height = ctx->swapchain.extent.height;
    size_t rgba_bytes = 0;
    size_t rgb_bytes = 0;
    size_t row_stride = 0;
    if (!VK_ImageBytes(width, height, 4, &rgba_bytes, "screenshot readback") ||
        !VK_ImageBytes(width, height, 3, &rgb_bytes, "screenshot RGB conversion") ||
        !VK_ArrayBytes((size_t)width, 3, &row_stride, "screenshot row stride")) {
        Com_EPrintf("Vulkan screenshot: image dimensions are too large\n");
        return;
    }
    if (width > (uint32_t)INT_MAX || height > (uint32_t)INT_MAX ||
        row_stride > (size_t)INT_MAX) {
        Com_EPrintf("Vulkan screenshot: image dimensions exceed PNG writer limits\n");
        return;
    }

    size_t pixel_count = rgba_bytes / 4;

    bool swap_red_blue;
    switch (ctx->swapchain.format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        swap_red_blue = true;
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        swap_red_blue = false;
        break;
    default:
        Com_EPrintf("Vulkan screenshot: unsupported swapchain format %d\n",
                    ctx->swapchain.format);
        return;
    }

    byte *rgb = Z_TagMalloc(rgb_bytes, TAG_RENDERER);
    if (!rgb) {
        Com_EPrintf("Vulkan screenshot: out of memory for RGB conversion\n");
        return;
    }

    void *mapped = NULL;
    if (!VK_Check(vkMapMemory(ctx->device, vk_screenshot_memory, 0,
                              VK_WHOLE_SIZE, 0, &mapped),
                  "vkMapMemory(screenshot)")) {
        Z_Free(rgb);
        return;
    }

    const byte *src = mapped;
    byte *dst = rgb;
    for (size_t i = 0; i < pixel_count; i++, src += 4, dst += 3) {
        if (swap_red_blue) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
        } else {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }
    vkUnmapMemory(ctx->device, vk_screenshot_memory);

    int ok;
    if (vk_screenshot_tga) {
        const int previous_tga_rle = stbi_write_tga_with_rle;
        stbi_write_tga_with_rle = 0;
        ok = stbi_write_tga(vk_screenshot_path, (int)width, (int)height, 3, rgb);
        stbi_write_tga_with_rle = previous_tga_rle;
    } else {
        ok = stbi_write_png(vk_screenshot_path, (int)width, (int)height, 3,
                            rgb, (int)row_stride);
    }
    Z_Free(rgb);

    if (!ok) {
        Com_EPrintf("Couldn't write %s\n", vk_screenshot_path);
        return;
    }

    Com_Printf("Wrote %s\n", vk_screenshot_path);
}

static void VK_RequestScreenshot(const char *extension, bool tga)
{
    if (!vk_state.initialized || !vk_state.ctx.swapchain.handle) {
        Com_EPrintf("No Vulkan swapchain available for screenshot.\n");
        return;
    }
    if (!vk_screenshot_supported) {
        Com_EPrintf("Vulkan swapchain does not support readback.\n");
        return;
    }
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [name]\n", Cmd_Argv(0));
        return;
    }

    char name[MAX_OSPATH];
    if (Cmd_Argc() > 1) {
        if (FS_NormalizePathBuffer(name, Cmd_Argv(1), sizeof(name)) >= sizeof(name)) {
            Com_EPrintf("Screenshot name too long.\n");
            return;
        }
        FS_CleanupPath(name);
    } else {
        Q_snprintf(name, sizeof(name), "worr_vk_%llu",
                   (unsigned long long)time(NULL));
    }

    if (vk_screenshot_dir && vk_screenshot_dir->string[0]) {
        char directory[MAX_OSPATH];
        if (Q_strlcpy(directory, vk_screenshot_dir->string,
                      sizeof(directory)) >= sizeof(directory)) {
            Com_EPrintf("Screenshot directory too long.\n");
            return;
        }
        for (char *cursor = directory; *cursor; ++cursor) {
            if (*cursor == '\\') {
                *cursor = '/';
            }
        }
        if (Q_snprintf(vk_screenshot_path, sizeof(vk_screenshot_path),
                       "%s/%s%s", directory, name, extension) >=
            sizeof(vk_screenshot_path)) {
            Com_EPrintf("Screenshot path too long.\n");
            return;
        }
    } else if (Q_snprintf(vk_screenshot_path, sizeof(vk_screenshot_path),
                          "%s/screenshots/%s%s", fs_gamedir, name,
                          extension) >= sizeof(vk_screenshot_path)) {
        Com_EPrintf("Screenshot path too long.\n");
        return;
    }

    int ret = FS_CreatePath(vk_screenshot_path);
    if (ret < 0) {
        Com_EPrintf("Couldn't create path for %s: %s\n", vk_screenshot_path,
                    Q_ErrorString(ret));
        return;
    }

    vk_screenshot_tga = tga;
    vk_screenshot_pending = true;
}

static void VK_ScreenShotPNG_f(void)
{
    VK_RequestScreenshot(".png", false);
}

static void VK_ScreenShotTGA_f(void)
{
    VK_RequestScreenshot(".tga", true);
}

static void VK_ImageBarrier(VkCommandBuffer cmd, VkImage image,
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
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_ImageBarrierRange(VkCommandBuffer cmd, VkImage image,
                                 VkImageLayout old_layout,
                                 VkImageLayout new_layout,
                                 VkAccessFlags src_access,
                                 VkAccessFlags dst_access,
                                 VkPipelineStageFlags src_stage,
                                 VkPipelineStageFlags dst_stage,
                                 uint32_t base_mip_level,
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
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

// The linear scene path never touches the acquired presentation image until
// the final composite. Transition it explicitly: the legacy scene-copy path
// used to leave the image in this layout as a side effect.
static void VK_PresentationImageToColorAttachment(VkCommandBuffer cmd,
                                                   uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    if (!cmd || image_index >= ctx->swapchain.image_count) {
        return;
    }
    const VkImageLayout old_layout = ctx->swapchain.image_presented &&
        ctx->swapchain.image_presented[image_index]
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    VK_ImageBarrier(cmd, ctx->swapchain.images[image_index], old_layout,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    0, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

// In the common fixed/adaptive LDR scaling case, the final shader composite
// would only linearly scale the native offscreen scene. A native image blit
// avoids the scene-copy allocation traffic and fullscreen draw while leaving
// every colour, HDR, liquid, and CRT path on its exact shader implementation.
static bool VK_BlitScaledSceneToPresentation(VkCommandBuffer cmd,
                                             uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    if (!cmd || !ctx->scaled_scene_blit_supported ||
        !frame->linear_scene_image ||
        image_index >= ctx->swapchain.image_count) {
        return false;
    }

    const VkImage presentation_image = ctx->swapchain.images[image_index];
    const VkImageLayout presentation_layout = ctx->swapchain.image_presented &&
        ctx->swapchain.image_presented[image_index]
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    VK_ImageBarrier(cmd, frame->linear_scene_image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
    VK_ImageBarrier(cmd, presentation_image, presentation_layout,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .srcOffsets = {
            { 0, 0, 0 },
            { (int32_t)ctx->scene_extent.width,
              (int32_t)ctx->scene_extent.height, 1 },
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .dstOffsets = {
            { 0, 0, 0 },
            { (int32_t)ctx->swapchain.extent.width,
              (int32_t)ctx->swapchain.extent.height, 1 },
        },
    };
    vkCmdBlitImage(cmd, frame->linear_scene_image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   presentation_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    // Restore the frame-slot target for a later reuse. The presentation image
    // returns to PRESENT so the existing UI overlay pass remains compatible.
    VK_ImageBarrier(cmd, frame->linear_scene_image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VK_ImageBarrier(cmd, presentation_image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    return true;
}

static void VK_BloomEmission_Record(VkCommandBuffer cmd,
                                    vk_frame_context_t *frame,
                                    const VkExtent2D *extent,
                                    bool draw_entities, bool before_liquid,
                                    bool sampled_depth_rim)
{
    vk_context_t *ctx = &vk_state.ctx;
    VkRenderPass render_pass = sampled_depth_rim
        ? ctx->bloom_rim_extract_render_pass : ctx->bloom_extract_render_pass;
    VkFramebuffer framebuffer = sampled_depth_rim
        ? frame->bloom_rim_emission_framebuffer
        : frame->bloom_emission_framebuffer;
    if (!cmd || !frame || !extent || !render_pass || !frame->bloom_emission_image ||
        !framebuffer) {
        return;
    }

    VK_ImageBarrier(
        cmd, frame->bloom_emission_image,
        frame->bloom_emission_initialized
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        sampled_depth_rim ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                          : frame->bloom_emission_initialized
                            ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        sampled_depth_rim ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          : frame->bloom_emission_initialized
                            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkClearValue clear_value = {
        .color = { { 0.0f, 0.0f, 0.0f, 1.0f } },
    };
    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {
            .extent = *extent,
        },
        .clearValueCount = sampled_depth_rim ? 0 : 1,
        .pClearValues = sampled_depth_rim ? NULL : &clear_value,
    };
    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    if (!sampled_depth_rim) {
        VK_World_RecordBloomEmission(cmd, extent);
    }
    if (draw_entities) {
        VK_Entity_RecordBloomEmission(cmd, extent, before_liquid,
                                      sampled_depth_rim);
    }
    vkCmdEndRenderPass(cmd);
    frame->bloom_emission_initialized = true;
}

static void VK_BloomDepthHackEmission_Record(VkCommandBuffer cmd,
                                             vk_frame_context_t *frame,
                                             const VkExtent2D *extent)
{
    vk_context_t *ctx = &vk_state.ctx;
    if (!cmd || !frame || !extent || !ctx->bloom_overlay_extract_render_pass ||
        !frame->bloom_emission_image || !frame->bloom_emission_framebuffer ||
        !frame->bloom_emission_initialized) {
        return;
    }

    VK_ImageBarrier(
        cmd, frame->bloom_emission_image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = ctx->bloom_overlay_extract_render_pass,
        .framebuffer = frame->bloom_emission_framebuffer,
        .renderArea = {
            .extent = *extent,
        },
    };
    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    VK_Entity_RecordDepthHackBloomEmission(cmd, extent);
    vkCmdEndRenderPass(cmd);
}

static void VK_DepthToShaderRead(VkCommandBuffer cmd,
                                 const vk_frame_context_t *frame)
{
    if (!cmd || !frame || !frame->depth_image || !frame->depth_sample_view) {
        return;
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame->depth_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format)
                    ? VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_DepthToAttachment(VkCommandBuffer cmd,
                                 const vk_frame_context_t *frame)
{
    if (!cmd || !frame || !frame->depth_image || !frame->depth_sample_view) {
        return;
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame->depth_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format)
                    ? VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_DepthAttachmentBarrier(VkCommandBuffer cmd,
                                      const vk_frame_context_t *frame)
{
    if (!cmd || !frame || !frame->depth_image) {
        return;
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame->depth_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format)
                    ? VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_SceneCopy_Record(VkCommandBuffer cmd, uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    if (!frame->liquid_scene_image ||
        image_index >= ctx->swapchain.image_count) {
        return;
    }

    VkImage swap_image = ctx->swapchain.images[image_index];
    VkImage scene_image = frame->liquid_scene_image;
    VK_ImageBarrier(
        cmd, swap_image,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VK_ImageBarrier(
        cmd, scene_image,
        frame->liquid_scene_initialized
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        frame->liquid_scene_initialized
            ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        frame->liquid_scene_initialized
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy copy = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .extent = {
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .depth = 1,
        },
    };
    vkCmdCopyImage(cmd, swap_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   scene_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VK_ImageBarrier(
        cmd, scene_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VK_ImageBarrier(
        cmd, swap_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    frame->liquid_scene_initialized = true;
}

static void VK_LinearSceneCopy_Record(VkCommandBuffer cmd,
                                      bool generate_exposure_mips)
{
    vk_context_t *ctx = &vk_state.ctx;
    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    if (!frame->linear_scene_image || !frame->linear_scene_copy_image) {
        return;
    }
    VK_ImageBarrier(cmd, frame->linear_scene_image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
    const uint32_t mip_levels = generate_exposure_mips
        ? max(ctx->linear_scene_mip_levels, 1u) : 1u;
    VK_ImageBarrier(cmd, frame->linear_scene_copy_image,
                    frame->linear_scene_copy_initialized
                        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    frame->linear_scene_copy_initialized
                        ? VK_ACCESS_SHADER_READ_BIT : 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    frame->linear_scene_copy_initialized
                        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy copy = {
        .srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .layerCount = 1 },
        .dstSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .layerCount = 1 },
        .extent = { ctx->scene_extent.width, ctx->scene_extent.height, 1 },
    };
    vkCmdCopyImage(cmd, frame->linear_scene_image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   frame->linear_scene_copy_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    if (mip_levels > 1) {
        VK_ImageBarrier(cmd, frame->linear_scene_copy_image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
        for (uint32_t level = 1; level < mip_levels; ++level) {
            VK_ImageBarrierRange(
                cmd, frame->linear_scene_copy_image,
                frame->linear_scene_copy_mips_initialized
                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                frame->linear_scene_copy_mips_initialized
                    ? VK_ACCESS_SHADER_READ_BIT : 0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                frame->linear_scene_copy_mips_initialized
                    ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                    : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, level, 1);
            const uint32_t src_width = max(1u,
                ctx->scene_extent.width >> (level - 1));
            const uint32_t src_height = max(1u,
                ctx->scene_extent.height >> (level - 1));
            const uint32_t dst_width = max(1u,
                ctx->scene_extent.width >> level);
            const uint32_t dst_height = max(1u,
                ctx->scene_extent.height >> level);
            VkImageBlit blit = {
                .srcSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = level - 1,
                    .layerCount = 1,
                },
                .srcOffsets = { { 0, 0, 0 },
                                { (int32_t)src_width, (int32_t)src_height, 1 } },
                .dstSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = level,
                    .layerCount = 1,
                },
                .dstOffsets = { { 0, 0, 0 },
                                { (int32_t)dst_width, (int32_t)dst_height, 1 } },
            };
            vkCmdBlitImage(cmd, frame->linear_scene_copy_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           frame->linear_scene_copy_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                           VK_FILTER_LINEAR);
            VK_ImageBarrierRange(
                cmd, frame->linear_scene_copy_image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                level, 1);
        }
        VK_ImageBarrierRange(
            cmd, frame->linear_scene_copy_image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, mip_levels);
        frame->linear_scene_copy_mips_initialized = true;
    } else {
        VK_ImageBarrier(cmd, frame->linear_scene_copy_image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        // Level-zero-only liquid sampling deliberately skips reduction. Keep
        // the unused levels in a valid layout nevertheless: descriptor
        // validation covers every level exposed by the post-process view.
        if (ctx->linear_scene_mip_levels > 1 &&
            !frame->linear_scene_copy_mips_initialized) {
            VK_ImageBarrierRange(
                cmd, frame->linear_scene_copy_image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 1,
                ctx->linear_scene_mip_levels - 1);
            frame->linear_scene_copy_mips_initialized = true;
        }
    }
    VK_ImageBarrier(cmd, frame->linear_scene_image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    frame->linear_scene_copy_initialized = true;
}

static void VK_LinearSceneDirectToShaderRead(VkCommandBuffer cmd)
{
    vk_context_t *ctx = &vk_state.ctx;
    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    if (!frame->linear_scene_image || !frame->linear_scene_descriptor_set) {
        return;
    }
    VK_ImageBarrier(cmd, frame->linear_scene_image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    frame->linear_scene_direct_sampled = true;
}

static void VK_LinearSceneRestoreColorAttachment(VkCommandBuffer cmd)
{
    vk_context_t *ctx = &vk_state.ctx;
    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    if (!frame->linear_scene_image || !frame->linear_scene_direct_sampled) {
        return;
    }
    VK_ImageBarrier(cmd, frame->linear_scene_image,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    frame->linear_scene_direct_sampled = false;
}

static void VK_RecordTearingDiagnostic(VkCommandBuffer cmd,
                                       uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    if (!cmd || !ctx->presentation_render_pass || !ctx->swapchain.extent.width ||
        !ctx->swapchain.extent.height ||
        ctx->current_frame >= ctx->frame_count ||
        image_index >= ctx->swapchain.image_count) {
        return;
    }

    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    if (!frame->framebuffers || !frame->framebuffers[image_index]) {
        return;
    }

    // Match GL_DrawTearing: alternate a full-frame white and red clear after
    // every scene/UI submission, immediately before presentation.
    const bool white = (++vk_showtearing_frame & 1u) != 0;
    VkClearValue clear_values[2] = {
        {
            .color = { { 1.0f, white ? 1.0f : 0.0f,
                         white ? 1.0f : 0.0f, white ? 1.0f : 0.0f } },
        },
        {
            .depthStencil = { 1.0f, 0 },
        },
    };
    VkRenderPassBeginInfo tearing_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = ctx->presentation_render_pass,
        .framebuffer = frame->framebuffers[image_index],
        .renderArea = {
            .extent = ctx->swapchain.extent,
        },
        .clearValueCount = q_countof(clear_values),
        .pClearValues = clear_values,
    };
    vkCmdBeginRenderPass(cmd, &tearing_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);
}

static bool VK_RecordCommandBuffer(VkCommandBuffer cmd, uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    const bool linear_scene = frame->linear_scene_image &&
        frame->linear_scene_copy_image && frame->linear_scene_framebuffer &&
        frame->linear_scene_copy_descriptor_set &&
        frame->linear_scene_copy_base_descriptor_set &&
        frame->linear_scene_descriptor_set;
    if (!frame->framebuffers || image_index >= ctx->swapchain.image_count ||
        !frame->framebuffers[image_index] ||
        (linear_scene && !frame->linear_scene_framebuffer)) {
        Com_SetLastError("Vulkan: frame-context framebuffer unavailable");
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    VkResult result = vkBeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkBeginCommandBuffer");
    }
    VK_Debug_BeginGpuFrame(cmd, ctx->current_frame);
    if (linear_scene) {
        VK_LinearSceneRestoreColorAttachment(cmd);
    }

    VkClearValue clear_values[2] = {
        {
            .color = { { 0.0f, 0.0f, 0.0f, 1.0f } },
        },
        {
            .depthStencil = { 1.0f, 0 },
        },
    };

    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = ctx->scene_render_pass,
        .framebuffer = linear_scene ? frame->linear_scene_framebuffer
                                    : frame->framebuffers[image_index],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = linear_scene ? ctx->scene_extent : ctx->swapchain.extent,
        },
        .clearValueCount = q_countof(clear_values),
        .pClearValues = clear_values,
    };

    // Current-frame dynamic streams are copied into device-local vertex/index
    // storage before any pass can bind them.
    VK_Entity_RecordUploads(cmd);
    VK_Shadow_RecordUploads(cmd);
    VK_UI_RecordUploads(cmd);
    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_UPLOAD);
    VK_Shadow_Record(cmd);
    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_SHADOW);
    VK_Entity_ResetFlareQueries(cmd);

    // Native menu previews are submitted after RmlUi has queued its draw
    // list. Overlay only the no-world subview entity pass after the UI so the
    // opaque shell background cannot hide it; the preserved entity scissor
    // keeps the model inside the authored preview surface.
    const bool entity_overlay = VK_Entity_IsNoWorldSubview();

    const bool depth_dof = !entity_overlay && VK_PostProcess_UsesDof();
    const bool draw_world = !vk_draw_world || vk_draw_world->integer;
    const bool draw_entities = (!entity_overlay || linear_scene) &&
        (!vk_draw_entities || vk_draw_entities->integer);
    const VkDescriptorSet scene_descriptor_set = linear_scene
        ? frame->linear_scene_copy_descriptor_set
        : frame->liquid_scene_descriptor_set;
    const VkDescriptorSet liquid_scene_descriptor_set = linear_scene
        ? frame->linear_scene_copy_base_descriptor_set
        : frame->liquid_scene_descriptor_set;
    const bool liquid_refraction = !entity_overlay && draw_world &&
        ctx->scene_load_render_pass && liquid_scene_descriptor_set &&
        VK_World_UsesRefraction();
    const bool direct_linear_presentation = linear_scene &&
        !liquid_refraction && !VK_PostProcess_RequiresSceneCopy();
    const bool postprocess_composite = VK_PostProcess_UsesCompositePass();
    const bool crt_postprocess = VK_PostProcess_UsesCrtPass();
    const bool final_postprocess = ctx->presentation_load_render_pass &&
        scene_descriptor_set &&
        VK_PostProcess_UsesFinalPass();
    const bool scaled_scene_blit = linear_scene &&
        !ctx->scene_is_float && ctx->scaled_scene_blit_supported &&
        (ctx->scene_extent.width != ctx->swapchain.extent.width ||
         ctx->scene_extent.height != ctx->swapchain.extent.height) &&
        !liquid_refraction && VK_PostProcess_AllowsScaledSceneBlit();
    const bool bloom_emission = final_postprocess &&
        VK_PostProcess_UsesBloomEmission() &&
        ctx->bloom_extract_render_pass && frame->bloom_emission_image &&
        frame->bloom_emission_framebuffer;

    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    if (draw_world) {
        if (liquid_refraction) {
            VK_World_RecordOpaque(cmd, &ctx->scene_extent);
        } else {
            VK_World_Record(cmd, &ctx->scene_extent);
        }
    }
    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_OPAQUE_WORLD);
    if (draw_entities) {
        if (liquid_refraction) {
            VK_Entity_RecordBeforeLiquid(cmd, &ctx->scene_extent);
        } else {
            VK_Entity_Record(cmd, &ctx->scene_extent);
        }
    }
    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_OPAQUE_ENTITY);
    if (!liquid_refraction) {
        VK_Debug_RecordShowTris(cmd, &ctx->scene_extent);
        VK_Debug_Record(cmd, &ctx->scene_extent);
        if (!final_postprocess && (!vk_draw_ui || vk_draw_ui->integer)) {
            VK_UI_Record(cmd, &ctx->swapchain.extent);
        }
    }
    vkCmdEndRenderPass(cmd);

    if (bloom_emission) {
        VK_BloomEmission_Record(cmd, frame, &ctx->scene_extent,
                                draw_entities, liquid_refraction, false);
        const bool sampled_rim_bloom = draw_entities &&
            ctx->bloom_rim_extract_render_pass &&
            frame->bloom_rim_emission_framebuffer &&
            VK_Entity_HasBloomRimDepthSampling(liquid_refraction);
        if (sampled_rim_bloom) {
            VK_DepthToShaderRead(cmd, frame);
            VK_BloomEmission_Record(cmd, frame, &ctx->scene_extent,
                                    true, liquid_refraction, true);
            VK_DepthToAttachment(cmd, frame);
        }
    }

    if (liquid_refraction) {
        if (linear_scene) {
            VK_LinearSceneCopy_Record(cmd, false);
        } else {
            VK_SceneCopy_Record(cmd, image_index);
        }
        VK_DepthAttachmentBarrier(cmd, frame);

        VkRenderPassBeginInfo liquid_info = render_info;
        liquid_info.renderPass = ctx->scene_load_render_pass;
        vkCmdBeginRenderPass(cmd, &liquid_info, VK_SUBPASS_CONTENTS_INLINE);
        VK_World_RecordAlpha(cmd, &ctx->scene_extent,
                             liquid_scene_descriptor_set);
        if (!vk_draw_entities || vk_draw_entities->integer) {
            VK_Entity_RecordAfterLiquid(cmd, &ctx->scene_extent);
        }
        VK_Debug_RecordShowTris(cmd, &ctx->scene_extent);
        VK_Debug_Record(cmd, &ctx->scene_extent);
        if (!final_postprocess && (!vk_draw_ui || vk_draw_ui->integer)) {
            VK_UI_Record(cmd, &ctx->swapchain.extent);
        }
        vkCmdEndRenderPass(cmd);
    }

    if (liquid_refraction && bloom_emission && draw_entities &&
        ctx->bloom_overlay_extract_render_pass &&
        VK_Entity_HasDepthHackBloomEmission()) {
        VK_BloomDepthHackEmission_Record(cmd, frame, &ctx->scene_extent);
    }

    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_SCENE);

    if (scaled_scene_blit && VK_BlitScaledSceneToPresentation(cmd, image_index)) {
        // The native transfer path already prepared presentation for the
        // standard UI overlay render pass below.
    } else if (final_postprocess) {
        // Copy the completed 3D scene (including any native liquid pass) and
        // overwrite the swapchain through native Vulkan composition. UI is
        // recorded only after the scene passes, so HUD/menu text remains sharp.
        if (direct_linear_presentation) {
            VK_LinearSceneDirectToShaderRead(cmd);
        } else if (linear_scene) {
            VK_LinearSceneCopy_Record(cmd,
                                      VK_PostProcess_UsesAutoExposure());
        } else {
            VK_SceneCopy_Record(cmd, image_index);
        }
        if (linear_scene && VK_PostProcess_UsesAutoExposure()) {
            VK_PostProcess_RecordAutoExposure(cmd);
        }
        if (depth_dof) {
            VK_DepthToShaderRead(cmd, frame);
            VK_PostProcess_RecordDof(cmd);
            VK_DepthToAttachment(cmd, frame);
        }
        VK_PostProcess_RecordBloom(cmd);

        if (postprocess_composite) {
            if (linear_scene) {
                VK_PresentationImageToColorAttachment(cmd, image_index);
            }
            VkRenderPassBeginInfo final_info = render_info;
            final_info.renderPass = ctx->presentation_load_render_pass;
            final_info.framebuffer = frame->framebuffers[image_index];
            vkCmdBeginRenderPass(cmd, &final_info, VK_SUBPASS_CONTENTS_INLINE);
            VK_PostProcess_RecordFinal(
                cmd, &ctx->swapchain.extent,
                direct_linear_presentation
                    ? frame->linear_scene_descriptor_set
                    : scene_descriptor_set,
                direct_linear_presentation);
            vkCmdEndRenderPass(cmd);
        }

        if (crt_postprocess) {
            // When a base composite ran, copy its result before the CRT pass;
            // for CRT-only frames the original scene copy is already the input.
            if (postprocess_composite) {
                VK_SceneCopy_Record(cmd, image_index);
            }
            VkRenderPassBeginInfo crt_info = render_info;
            crt_info.renderPass = ctx->presentation_load_render_pass;
            crt_info.framebuffer = frame->framebuffers[image_index];
            vkCmdBeginRenderPass(cmd, &crt_info, VK_SUBPASS_CONTENTS_INLINE);
            VK_PostProcess_RecordCrt(
                cmd, &ctx->swapchain.extent,
                frame->liquid_scene_descriptor_set);
            vkCmdEndRenderPass(cmd);
        }
    }

    if (final_postprocess) {
        VkRenderPassBeginInfo ui_overlay_info = render_info;
        ui_overlay_info.renderPass = ctx->presentation_overlay_render_pass;
        ui_overlay_info.framebuffer = frame->framebuffers[image_index];
        vkCmdBeginRenderPass(cmd, &ui_overlay_info, VK_SUBPASS_CONTENTS_INLINE);
        if (!vk_draw_ui || vk_draw_ui->integer) {
            VK_UI_Record(cmd, &ctx->swapchain.extent);
        }
        vkCmdEndRenderPass(cmd);
    }

    if (entity_overlay && !linear_scene &&
        (!vk_draw_entities || vk_draw_entities->integer)) {
        VkRenderPassBeginInfo overlay_info = render_info;
        overlay_info.renderPass = ctx->presentation_overlay_render_pass;
        overlay_info.framebuffer = frame->framebuffers[image_index];
        vkCmdBeginRenderPass(cmd, &overlay_info, VK_SUBPASS_CONTENTS_INLINE);
        VK_Entity_Record(cmd, &ctx->swapchain.extent);
        vkCmdEndRenderPass(cmd);
    }

    if (vk_showtearing && vk_showtearing->integer) {
        VK_RecordTearingDiagnostic(cmd, image_index);
    }

    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_POSTPROCESS);

    if (vk_screenshot_armed) {
        VK_Screenshot_RecordCopy(cmd, image_index);
    }

    VK_Debug_EndGpuFrame(cmd);

    result = vkEndCommandBuffer(cmd);
    return VK_Check(result, "vkEndCommandBuffer");
}

static bool VK_WaitForFrame(vk_context_t *ctx, uint32_t frame_index,
                            const char *what)
{
    if (!ctx || frame_index >= ctx->frame_count) {
        Com_SetLastError("Vulkan: invalid frame context");
        return false;
    }

    vk_frame_context_t *frame = &ctx->frames[frame_index];
    if (!frame->submitted) {
        return true;
    }

    VkResult result = vkWaitForFences(ctx->device, 1, &frame->in_flight_fence, VK_TRUE,
                                      UINT64_MAX);
    if (result == VK_SUCCESS) {
        VK_Debug_ResolveGpuFrame(frame_index);
        frame->submitted = false;
        return true;
    }

    frame->submitted = false;
    return VK_Check(result, what);
}

static bool VK_WaitForSubmittedFrames(vk_context_t *ctx, const char *what)
{
    if (!ctx) {
        return false;
    }
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        if (!VK_WaitForFrame(ctx, i, what)) {
            return false;
        }
    }
    return true;
}

static void VK_UpdateLinearSceneMipLevels(vk_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    ctx->linear_scene_mip_levels = 1;
    cvar_t *hdr_auto = Cvar_Get("vk_hdr_auto_exposure", "0",
                                CVAR_ARCHIVE | CVAR_RENDERER);
    if (!ctx->scene_is_float || !ctx->linear_scene_mips_supported ||
        !hdr_auto || !hdr_auto->integer) {
        return;
    }

    uint32_t max_dimension = max(ctx->scene_extent.width,
                                 ctx->scene_extent.height);
    while (max_dimension > 1) {
        ctx->linear_scene_mip_levels++;
        max_dimension >>= 1;
    }
}

// Resize only the native offscreen scene family. Presentation images, command
// buffers, synchronization, and native UI resources remain valid because the
// swapchain extent and attachment formats do not change. The first entry into
// (and final exit from) offscreen rendering still goes through the full
// swapchain path, since that changes the scene render-pass final layout.
static bool VK_RecreateSceneTargets(void)
{
    vk_context_t *ctx = &vk_state.ctx;
    if (!ctx->device || !ctx->swapchain.handle || !ctx->scene_offscreen_supported) {
        return false;
    }

    const VkExtent2D requested_extent =
        VK_ResolutionScaleExtent(ctx->swapchain.extent);
    if (requested_extent.width == ctx->scene_extent.width &&
        requested_extent.height == ctx->scene_extent.height) {
        return true;
    }

    // Direct presentation has a different scene-pass final layout. Let the
    // established full rebuild perform that transition before attempting an
    // extent-only target refresh.
    if (!ctx->frames[0].linear_scene_image) {
        vk_state.swapchain_dirty = true;
        return VK_RecreateSwapchain(r_config.width, r_config.height);
    }

    if (!VK_WaitForSubmittedFrames(
            ctx, "vkWaitForFences(resolution-scale target rebuild)")) {
        return false;
    }

    // All retained render passes remain attachment-compatible: resolution is
    // dynamic, while color/depth formats stay fixed. Rebuild just the images
    // and framebuffers that name the old scene extent.
    VK_DestroyBloomEmissionImages(ctx);
    VK_DestroyLinearSceneResources(ctx);
    ctx->scene_extent = requested_extent;
    VK_UpdateLinearSceneMipLevels(ctx);

    if (!VK_CreateLinearSceneResources(ctx) ||
        !VK_CreateLinearSceneFramebuffers(ctx)) {
        Com_WPrintf("Vulkan: scaled scene target refresh failed; rebuilding the full swapchain.\n");
        vk_state.swapchain_dirty = true;
        return VK_RecreateSwapchain(r_config.width, r_config.height);
    }

    // Authored bloom remains optional, but when supported its extent follows
    // the refreshed native scene target without replacing swapchain state.
    VK_CreateBloomEmissionResources(ctx);
    VK_PostProcess_RefreshSceneResources(ctx);
    return true;
}

static bool VK_DrawFrame(void)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (!ctx->swapchain.handle || !ctx->frame_count ||
        ctx->current_frame >= ctx->frame_count) {
        return false;
    }

    const uint32_t frame_index = ctx->current_frame;
    vk_frame_context_t *frame = &ctx->frames[frame_index];
    VkResult result = VK_SUCCESS;
    if (VK_PostProcess_NeedsSafeResourceUpdate() &&
        !VK_WaitForSubmittedFrames(ctx,
                                   "vkWaitForFences(bloom resource rebuild)")) {
        return false;
    }
    VK_PostProcess_PrepareFrame();

    uint32_t image_index = 0;
    result = vkAcquireNextImageKHR(ctx->device, ctx->swapchain.handle, UINT64_MAX,
                                   frame->image_available, VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vk_state.swapchain_dirty = true;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return VK_Check(result, "vkAcquireNextImageKHR");
    }
    if (image_index >= ctx->swapchain.image_count ||
        !ctx->swapchain.image_frame_slots) {
        Com_SetLastError("Vulkan: acquired image has no frame ownership state");
        return false;
    }
    uint32_t image_frame = ctx->swapchain.image_frame_slots[image_index];
    if (image_frame != VK_INVALID_FRAME_SLOT && image_frame != frame_index &&
        !VK_WaitForFrame(ctx, image_frame,
                         "vkWaitForFences(acquired swapchain image)")) {
        return false;
    }

    vk_screenshot_armed = false;
    if (vk_screenshot_pending) {
        size_t needed_bytes = 0;
        if (vk_screenshot_supported &&
            VK_ImageBytes(ctx->swapchain.extent.width, ctx->swapchain.extent.height,
                          4, &needed_bytes, "screenshot readback") &&
            VK_Screenshot_EnsureBuffer((VkDeviceSize)needed_bytes)) {
            vk_screenshot_armed = true;
        } else {
            Com_EPrintf("Vulkan screenshot: readback unavailable\n");
            vk_screenshot_pending = false;
        }
    }

    VkCommandBuffer cmd = frame->command_buffer;
    VkSemaphore render_finished = ctx->swapchain.render_finished[image_index];
    vkResetCommandBuffer(cmd, 0);
    if (!VK_RecordCommandBuffer(cmd, image_index))
        return false;

    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->image_available,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished,
    };

    result = vkResetFences(ctx->device, 1, &frame->in_flight_fence);
    if (result != VK_SUCCESS) {
        frame->submitted = false;
        return VK_Check(result, "vkResetFences");
    }

    result = vkQueueSubmit(ctx->graphics_queue, 1, &submit_info,
                           frame->in_flight_fence);
    if (result != VK_SUCCESS) {
        frame->submitted = false;
        return VK_Check(result, "vkQueueSubmit");
    }
    frame->submitted = true;
    ctx->swapchain.image_frame_slots[image_index] = frame_index;
    VK_Debug_MarkGpuFrameSubmitted(frame_index);
    ctx->current_frame = (frame_index + 1) % ctx->frame_count;

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_finished,
        .swapchainCount = 1,
        .pSwapchains = &ctx->swapchain.handle,
        .pImageIndices = &image_index,
    };

    result = vkQueuePresentKHR(ctx->graphics_queue, &present_info);

    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) &&
        ctx->swapchain.image_presented) {
        ctx->swapchain.image_presented[image_index] = true;
    }

    if (vk_screenshot_armed) {
        // The submitted command buffer already executed the readback copy;
        // finish the file write even if the present was suboptimal.
        VK_Screenshot_Complete();
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        vk_state.swapchain_dirty = true;
        return false;
    }

    return VK_Check(result, "vkQueuePresentKHR");
}

static void VK_DestroyContext(void)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (ctx->device) {
        vkDeviceWaitIdle(ctx->device);
    }

    VK_Debug_Shutdown(ctx);
    VK_Entity_Shutdown(ctx);
    VK_PostProcess_Shutdown(ctx);
    VK_World_Shutdown(ctx);
    VK_UI_Shutdown(ctx);
    VK_Screenshot_DestroyBuffer();
    vk_screenshot_armed = false;
    vk_screenshot_pending = false;
    VK_DestroySwapchain(ctx);

    if (ctx->command_pool) {
        vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);
        ctx->command_pool = VK_NULL_HANDLE;
    }

    if (ctx->device) {
        vkDestroyDevice(ctx->device, NULL);
        ctx->device = VK_NULL_HANDLE;
    }

    VK_DestroySurfaceHandle(ctx);

    if (ctx->instance) {
        vkDestroyInstance(ctx->instance, NULL);
        ctx->instance = VK_NULL_HANDLE;
    }

    ctx->physical_device = VK_NULL_HANDLE;
    ctx->graphics_queue = VK_NULL_HANDLE;
    ctx->graphics_queue_family = 0;
}

static void VK_ShadowDump_f(void)
{
    shadow_frontend_policy_t policy;
    ShadowFrontend_PolicyFromCvars(&vk_shadow_frontend_cvars, &policy);
    int focus = Cmd_Argc() > 1 ? Q_atoi(Cmd_Argv(1)) : -1;
    ShadowFrontend_Dump(&vk_shadow_frontend, &policy, &vk_shadow_backend_ops,
                        focus);
}

bool R_Init(bool total)
{
    if (!vk_draw_world) {
        vk_draw_world = Cvar_Get("vk_draw_world", "1", 0);
    }
    if (!vk_draw_entities) {
        vk_draw_entities = Cvar_Get("vk_draw_entities", "1", 0);
    }
    if (!vk_draw_ui) {
        vk_draw_ui = Cvar_Get("vk_draw_ui", "1", 0);
    }
    if (!vk_showtearing) {
        vk_showtearing = Cvar_Get("vk_showtearing", "0", CVAR_CHEAT);
    }
    if (!vk_damageblend_frac) {
        vk_damageblend_frac = Cvar_Get("gl_damageblend_frac", "0.2", 0);
    }
    if (!vk_screenshot_dir) {
        vk_screenshot_dir = Cvar_Get("r_screenshot_dir", "", CVAR_NOARCHIVE);
    }
    if (!vk_resolutionscale) {
        vk_resolutionscale = Cvar_Get("r_resolutionscale", "0", CVAR_USERINFO);
        vk_resolutionscale_aggressive = Cvar_Get(
            "r_resolutionscale_aggressive", "0", CVAR_ARCHIVE);
        vk_resolutionscale_fixedscale_h = Cvar_Get(
            "r_resolutionscale_fixedscale_h", "1.0", CVAR_SERVERINFO);
        vk_resolutionscale_fixedscale_w = Cvar_Get(
            "r_resolutionscale_fixedscale_w", "1.0", CVAR_SERVERINFO);
        vk_resolutionscale_gooddrawtime = Cvar_Get(
            "r_resolutionscale_gooddrawtime", "0.9", CVAR_SERVERINFO);
        vk_resolutionscale_increasespeed = Cvar_Get(
            "r_resolutionscale_increasespeed", "0.1", CVAR_SERVERINFO);
        vk_resolutionscale_lowerspeed = Cvar_Get(
            "r_resolutionscale_lowerspeed", "0.1", CVAR_SERVERINFO);
        vk_resolutionscale_numframesbeforelowering = Cvar_Get(
            "r_resolutionscale_numframesbeforelowering", "20", CVAR_USERINFO);
        vk_resolutionscale_numframesbeforeraising = Cvar_Get(
            "r_resolutionscale_numframesbeforeraising", "200", CVAR_USERINFO);
        vk_resolutionscale_targetdrawtime = Cvar_Get(
            "r_resolutionscale_targetdrawtime", "1.125", CVAR_SERVERINFO);
    }
    if (!vk_shadow_frontend_cvars_registered) {
        ShadowFrontend_RegisterCvars(&vk_shadow_frontend_cvars, "vk");
        Cmd_AddCommand("r_shadow_dump", VK_ShadowDump_f);
        Cmd_AddCommand("vk_shadow_dump", VK_ShadowDump_f);
        Cmd_AddCommand("screenshot", VK_ScreenShotPNG_f);
        Cmd_AddCommand("screenshotpng", VK_ScreenShotPNG_f);
        Cmd_AddCommand("screenshottga", VK_ScreenShotTGA_f);
        vk_shadow_frontend_cvars_registered = true;
    }

    if (!total) {
        vk_state.swapchain_dirty = true;
        return vk_state.initialized;
    }

    if (total && !vk_state.initialized) {
        VK_InitPalette();
        ShadowFrontend_Init(&vk_shadow_frontend);
    }

    Com_Printf("------- VK_Init -------\n");
    Com_Printf("Using video driver: %s\n", vid->name);

    if (!vid->init()) {
        return false;
    }

    vid_native_window_t native;
    if (!VK_QueryNativeWindow(&native)) {
        vid->shutdown();
        return false;
    }

    vk_state.native_window = native;

    if (!VK_CreateInstance()) {
        vid->shutdown();
        return false;
    }

    if (!VK_CreateSurface()) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_SelectPhysicalDevice()) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_CreateDevice()) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_CreateCommandPool(&vk_state.ctx)) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_Shadow_Init(&vk_state.ctx)) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_UI_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_PostProcess_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_World_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_Entity_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_Debug_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    vk_state.initialized = true;
    vk_state.swapchain_dirty = true;

    if (!VK_RecreateSwapchain(r_config.width, r_config.height)) {
        vk_state.swapchain_dirty = true;
    }

    Com_Printf("----------------------\n");

    return true;
}

void R_Shutdown(bool total)
{
    if (!vk_state.initialized) {
        return;
    }

    if (!total) {
        vk_state.swapchain_dirty = true;
        return;
    }

    // Shadow descriptors can still be referenced by the final submitted
    // command buffer. Drain the device before tearing that subsystem down;
    // VK_DestroyContext's wait occurs after this call and is therefore too
    // late for descriptor lifetime validation.
    if (vk_state.ctx.device) {
        vkDeviceWaitIdle(vk_state.ctx.device);
    }
    VK_Shadow_Shutdown(&vk_state.ctx);
    VK_DestroyContext();
    ShadowFrontend_Shutdown(&vk_shadow_frontend);
    Cmd_RemoveCommand("r_shadow_dump");
    Cmd_RemoveCommand("vk_shadow_dump");
    Cmd_RemoveCommand("screenshot");
    Cmd_RemoveCommand("screenshotpng");
    Cmd_RemoveCommand("screenshottga");
    vk_shadow_frontend_cvars_registered = false;
    vid->shutdown();

    memset(&vk_state, 0, sizeof(vk_state));
}

void R_BeginRegistration(const char *map)
{
    VK_Entity_BeginRegistration();
    VK_World_BeginRegistration(map);
}

qhandle_t R_RegisterModel(const char *name)
{
    return VK_Entity_RegisterModel(name);
}

qhandle_t R_RegisterImage(const char *name, imagetype_t type, imageflags_t flags)
{
    return VK_UI_RegisterImage(name, type, flags);
}

qhandle_t R_RegisterRawImage(const char *name, int width, int height, byte *pic,
                             imagetype_t type, imageflags_t flags)
{
    return VK_UI_RegisterRawImage(name, width, height, pic, type, flags);
}

void R_UnregisterImage(qhandle_t handle)
{
    if (vk_state.initialized && vk_state.ctx.device &&
        !VK_WaitForSubmittedFrames(&vk_state.ctx,
                                   "vkWaitForFences(unregister image)")) {
        return;
    }

    VK_UI_UnregisterImage(handle);
}

bool R_RmlUiDrawGeometry(const renderer_rmlui_vertex_t *vertices,
                         size_t vertex_count,
                         const uint32_t *indices,
                         size_t index_count,
                         float translation_x,
                         float translation_y,
                         qhandle_t texture)
{
    return VK_UI_DrawRmlGeometry(vertices, vertex_count, indices, index_count,
                                 translation_x, translation_y, texture);
}

void R_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis)
{
    Com_DPrintf("VK R_SetSky: name=%s rotate=%.2f autorotate=%d axis=(%.2f %.2f %.2f)\n",
                (name && *name) ? name : "<empty>",
                rotate, autorotate ? 1 : 0,
                axis ? axis[0] : 0.0f,
                axis ? axis[1] : 0.0f,
                axis ? axis[2] : 0.0f);
    VK_World_SetSky(name, rotate, autorotate, axis);
}

void R_EndRegistration(void)
{
    VK_World_EndRegistration();
    VK_Entity_EndRegistration();
}

void R_RenderFrame(const refdef_t *fd)
{
    VK_Debug_SetRefdef(fd);
    VK_Debug_SetSceneCounts((uint32_t)max(fd->num_entities, 0),
                            (uint32_t)max(fd->num_dlights, 0),
                            (uint32_t)max(fd->num_particles, 0));

    shadow_frontend_policy_t shadow_policy;
    ShadowFrontend_PolicyFromCvars(&vk_shadow_frontend_cvars, &shadow_policy);
    shadow_policy.max_resolution =
        min(shadow_policy.max_resolution, VK_SHADOW_MAX_RESOLUTION);
    shadow_policy.default_resolution =
        min(shadow_policy.default_resolution, VK_SHADOW_MAX_RESOLUTION);
    shadow_policy.sun_resolution =
        min(shadow_policy.sun_resolution, VK_SHADOW_MAX_RESOLUTION);
    int shadow_sun_pages = shadow_policy.sun_enabled
        ? shadow_policy.sun_cascades
        : 0;
    int shadow_local_pages = max(0, VK_SHADOW_MAX_PAGES - shadow_sun_pages);
    shadow_policy.max_lights =
        min(shadow_policy.max_lights,
            shadow_local_pages / SHADOW_FRONTEND_POINT_FACES);
    ShadowFrontend_BuildFrame(&vk_shadow_frontend, fd, VK_World_GetBsp(),
                              &shadow_policy, &vk_shadow_backend_ops);
    VK_Shadow_UpdateDlights(fd);

    VK_World_RenderFrame(fd);
    VK_PostProcess_RenderFrame(fd);
    VK_Entity_RenderFrame(fd);
    VK_PostProcess_SetBloomAuthoredEmission(
        VK_World_HasBloomEmission() || VK_Entity_HasBloomEmission());

    // Full-screen liquid/powerup and damage blends layer between the 3D view
    // and the HUD, mirroring the GL renderer's GL_Blend pass.
    VK_UI_DrawScreenBlend(fd, vk_damageblend_frac
                          ? Cvar_ClampValue(vk_damageblend_frac, 0.0f, 0.5f)
                          : 0.2f);
}

void R_LightPoint(const vec3_t origin, vec3_t light)
{
    VK_World_LightPoint(origin, light);
}

void R_SetClipRect(const clipRect_t *clip)
{
    VK_UI_SetClipRect(clip);
}

float R_ClampScale(cvar_t *var)
{
    return VK_UI_ClampScale(var);
}

void R_SetScale(float scale)
{
    VK_UI_SetScale(scale);
}

static inline color_t vk_draw_resolve_color(int flags, color_t color)
{
    if (flags & (UI_ALTCOLOR | UI_XORCOLOR)) {
        color_t alt = COLOR_RGB(255, 255, 0);
        alt.a = color.a;
        return alt;
    }
    return color;
}

static inline void vk_draw_char(int x, int y, int w, int h, int flags, int c,
                                color_t color, qhandle_t font)
{
    if ((c & 127) == 32)
        return;

    if (flags & UI_ALTCOLOR)
        c |= 0x80;
    if (flags & UI_XORCOLOR)
        c ^= 0x80;

    if (c >> 7)
        color = COLOR_SETA_U8(COLOR_WHITE, color.a);

    float s = (c & 15) * 0.0625f;
    float t = (c >> 4) * 0.0625f;
    float eps = 1e-5f;

    if ((flags & UI_DROPSHADOW) && c != 0x83) {
        color_t shadow = COLOR_A(color.a);
        VK_UI_DrawStretchSubPic(x + 1, y + 1, w, h,
                                s + eps, t + eps,
                                s + 0.0625f - eps, t + 0.0625f - eps,
                                shadow, font);
    }

    VK_UI_DrawStretchSubPic(x, y, w, h,
                            s + eps, t + eps,
                            s + 0.0625f - eps, t + 0.0625f - eps,
                            color, font);
}

void R_DrawChar(int x, int y, int flags, int ch, color_t color, qhandle_t font)
{
    vk_draw_char(x, y, CONCHAR_WIDTH, CONCHAR_HEIGHT, flags, ch & 255, color, font);
}

void R_DrawStretchChar(int x, int y, int w, int h, int flags, int ch, color_t color, qhandle_t font)
{
    vk_draw_char(x, y, w, h, flags, ch & 255, color, font);
}

int R_DrawStringStretch(int x, int y, int scale, int flags, size_t maxChars,
                        const char *string, color_t color, qhandle_t font)
{
    if (!string)
        return x;

    int sx = x;
    size_t remaining = maxChars;
    bool use_color_codes = Com_HasColorEscape(string, maxChars);
    int draw_flags = flags;
    color_t base_color = color;
    if (use_color_codes) {
        base_color = vk_draw_resolve_color(flags, color);
        draw_flags &= ~(UI_ALTCOLOR | UI_XORCOLOR);
    }
    color_t draw_color = use_color_codes ? base_color : color;

    while (remaining && *string) {
        if (use_color_codes) {
            color_t parsed;
            if (Com_ParseColorEscape(&string, &remaining, base_color, &parsed)) {
                draw_color = parsed;
                continue;
            }
        }

        byte ch = *string++;
        remaining--;

        if ((flags & UI_MULTILINE) && ch == '\n') {
            y += CONCHAR_HEIGHT * max(scale, 1) + 1;
            x = sx;
            continue;
        }

        vk_draw_char(x, y, CONCHAR_WIDTH * max(scale, 1), CONCHAR_HEIGHT * max(scale, 1),
                     draw_flags, ch, draw_color, font);
        x += CONCHAR_WIDTH * max(scale, 1);
    }

    return x;
}

const kfont_char_t *SCR_KFontLookup(const kfont_t *kfont, uint32_t codepoint)
{
    if (!kfont)
        return NULL;

    if (codepoint < KFONT_ASCII_MIN || codepoint > KFONT_ASCII_MAX)
        return NULL;

    const kfont_char_t *ch = &kfont->chars[codepoint - KFONT_ASCII_MIN];

    if (!ch->w)
        return NULL;

    return ch;
}

void SCR_LoadKFont(kfont_t *font, const char *filename)
{
    static cvar_t *cl_debug_fonts;
    if (!cl_debug_fonts)
        cl_debug_fonts = Cvar_Get("cl_debug_fonts", "1", 0);
    const bool debug_fonts = cl_debug_fonts && cl_debug_fonts->integer;

    memset(font, 0, sizeof(*font));

    char *buffer;

    if (FS_LoadFile(filename, (void **) &buffer) < 0) {
        if (debug_fonts) {
            Com_LPrintf(PRINT_ALL, "Font: SCR_LoadKFont \"%s\" failed: %s\n",
                        filename ? filename : "<null>", Com_GetLastError());
        }
        return;
    }

    if (debug_fonts) {
        Com_LPrintf(PRINT_ALL, "Font: SCR_LoadKFont \"%s\"\n",
                    filename ? filename : "<null>");
    }

    const char *data = buffer;

    while (true) {
        const char *token = COM_Parse(&data);

        if (!*token)
            break;

        if (!strcmp(token, "texture")) {
            token = COM_Parse(&data);
            font->pic = R_RegisterFont(va("/%s", token));
            if (debug_fonts) {
                Com_LPrintf(PRINT_ALL, "Font: kfont texture \"%s\" handle=%d\n",
                            token ? token : "<null>", font->pic);
            }
        } else if (!strcmp(token, "unicode")) {
        } else if (!strcmp(token, "mapchar")) {
            token = COM_Parse(&data);

            while (true) {
                token = COM_Parse(&data);

                if (!*token || !strcmp(token, "}"))
                    break;

                uint32_t codepoint = strtoul(token, NULL, 10);
                uint32_t x = strtoul(COM_Parse(&data), NULL, 10);
                uint32_t y = strtoul(COM_Parse(&data), NULL, 10);
                uint32_t w = strtoul(COM_Parse(&data), NULL, 10);
                uint32_t h = strtoul(COM_Parse(&data), NULL, 10);
                COM_Parse(&data);

                if (codepoint >= KFONT_ASCII_MIN && codepoint <= KFONT_ASCII_MAX) {
                    size_t char_index = (size_t)(codepoint - KFONT_ASCII_MIN);
                    font->chars[char_index].x = x;
                    font->chars[char_index].y = y;
                    font->chars[char_index].w = w;
                    font->chars[char_index].h = h;

                    font->line_height = max(font->line_height, h);
                }
            }
        }
    }

    int pic_w = 0;
    int pic_h = 0;
    if (font->pic && R_GetPicSize(&pic_w, &pic_h, font->pic) && pic_w > 0 && pic_h > 0) {
        font->sw = 1.0f / pic_w;
        font->sh = 1.0f / pic_h;
    }

    FS_FreeFile(buffer);

    if (debug_fonts) {
        Com_LPrintf(PRINT_ALL, "Font: kfont \"%s\" loaded line_height=%d handle=%d\n",
                    filename ? filename : "<null>", font->line_height, font->pic);
    }
}

int R_DrawKFontChar(int x, int y, int scale, int flags, uint32_t codepoint, color_t color, const kfont_t *kfont)
{
    const kfont_char_t *ch = SCR_KFontLookup(kfont, codepoint);
    if (!ch || !kfont || !kfont->pic || kfont->sw <= 0.0f || kfont->sh <= 0.0f)
        return 0;

    int draw_scale = max(scale, 1);
    int w = ch->w * draw_scale;
    int h = ch->h * draw_scale;

    float s = ch->x * kfont->sw;
    float t = ch->y * kfont->sh;
    float sw = ch->w * kfont->sw;
    float sh = ch->h * kfont->sh;

    if (flags & UI_DROPSHADOW) {
        color_t shadow = COLOR_A(color.a);
        int shadow_offset = draw_scale;
        VK_UI_DrawStretchSubPic(x + shadow_offset, y + shadow_offset, w, h,
                                s, t, s + sw, t + sh,
                                shadow, kfont->pic);
    }

    VK_UI_DrawStretchSubPic(x, y, w, h, s, t, s + sw, t + sh, color, kfont->pic);
    return ch->w * draw_scale;
}

bool R_GetPicSize(int *w, int *h, qhandle_t pic)
{
    return VK_UI_GetPicSize(w, h, pic);
}

void R_DrawPic(int x, int y, color_t color, qhandle_t pic)
{
    VK_UI_DrawPic(x, y, color, pic);
}

void R_DrawStretchPic(int x, int y, int w, int h, color_t color, qhandle_t pic)
{
    VK_UI_DrawStretchPic(x, y, w, h, color, pic);
}

void R_DrawStretchSubPic(int x, int y, int w, int h,
                         float s1, float t1, float s2, float t2,
                         color_t color, qhandle_t pic)
{
    VK_UI_DrawStretchSubPic(x, y, w, h, s1, t1, s2, t2, color, pic);
}

void R_DrawStretchRotatePic(int x, int y, int w, int h, color_t color, float angle,
                            int pivot_x, int pivot_y, qhandle_t pic)
{
    VK_UI_DrawStretchRotatePic(x, y, w, h, color, angle, pivot_x, pivot_y, pic);
}

void R_DrawKeepAspectPic(int x, int y, int w, int h, color_t color, qhandle_t pic)
{
    VK_UI_DrawKeepAspectPic(x, y, w, h, color, pic);
}

void R_DrawStretchRaw(int x, int y, int w, int h)
{
    VK_UI_DrawStretchRaw(x, y, w, h);
}

void R_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic)
{
    VK_UI_UpdateRawPic(pic_w, pic_h, pic);
}

bool R_UpdateImageRGBA(qhandle_t handle, int width, int height, const byte *pic)
{
    return VK_UI_UpdateImageRGBA(handle, width, height, pic);
}

void R_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
    VK_UI_TileClear(x, y, w, h, pic);
}

void R_DrawFill8(int x, int y, int w, int h, int c)
{
    VK_UI_DrawFill8(x, y, w, h, c);
}

void R_DrawFill32(int x, int y, int w, int h, color_t color)
{
    VK_UI_DrawFill32(x, y, w, h, color);
}

void R_BeginFrame(void)
{
    if (!vk_state.initialized)
        return;

    VK_UpdateResolutionScale(r_config.width, r_config.height);
    vk_context_t *ctx = &vk_state.ctx;
    const VkExtent2D requested_scene_extent =
        VK_ResolutionScaleExtent(ctx->swapchain.extent);

    if (vk_state.swapchain_dirty) {
        if (!VK_RecreateSwapchain(r_config.width, r_config.height))
            return;
    } else if (ctx->swapchain.handle && ctx->scene_offscreen_supported &&
               (requested_scene_extent.width != ctx->scene_extent.width ||
                requested_scene_extent.height != ctx->scene_extent.height) &&
               !VK_RecreateSceneTargets()) {
        return;
    }

    if (!ctx->frame_count ||
        !VK_WaitForFrame(ctx, ctx->current_frame,
                         "vkWaitForFences(begin frame)")) {
        return;
    }

    vk_frame_begin_us = VK_HighResMicroseconds();
    VK_Debug_BeginFrame();
    VK_UI_BeginFrame();
}

void R_EndFrame(void)
{
    if (!vk_state.initialized)
        return;

    if (vk_state.swapchain_dirty) {
        if (!VK_RecreateSwapchain(r_config.width, r_config.height))
            return;
    }

    VK_UI_EndFrame();
    if (!VK_DrawFrame()) {
        const char *error = Com_GetLastError();
        if (error && *error) {
            Com_WPrintf("Vulkan: frame submission failed: %s\n", error);
        }
    }
    VK_Debug_EndFrame((float)(VK_HighResMicroseconds() - vk_frame_begin_us) /
                      1000.0f);
    VK_RecordResolutionScaleTime();
}

void R_ModeChanged(int width, int height, int flags)
{
    r_config.width = width;
    r_config.height = height;
    r_config.flags = flags;

    if (!vk_state.initialized)
        return;

    if (vk_state.ctx.device) {
        vkDeviceWaitIdle(vk_state.ctx.device);
    }

    VK_PostProcess_DestroySwapchainResources(&vk_state.ctx);
    VK_DestroySwapchain(&vk_state.ctx);

    VK_DestroySurfaceHandle(&vk_state.ctx);

    vid_native_window_t native;
    if (!VK_QueryNativeWindow(&native)) {
        vk_state.swapchain_dirty = true;
        return;
    }

    vk_state.native_window = native;

    if (!VK_CreateSurface()) {
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_CreateSwapchain(width, height)) {
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_World_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_Entity_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_Debug_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_UI_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_PostProcess_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    VK_Debug_UpdateCapabilities(
        vk_screenshot_supported,
        VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format),
        vk_dof_supported);

    vk_state.swapchain_dirty = false;
}

bool R_VideoSync(void)
{
    return true;
}

void GL_ExpireDebugObjects(void)
{
    R_ExpireDebugLines();
}

bool R_SupportsPerPixelLighting(void)
{
    return true;
}

r_opengl_config_t R_GetGLConfig(void)
{
    bool stencil_available = vk_state.initialized &&
        VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format);
    r_opengl_config_t cfg = {
        .colorbits = 24,
        .depthbits = 24,
        .stencilbits = stencil_available ? 8 : 0,
        .multisamples = 0,
        .debug = false,
        .profile = QGL_PROFILE_NONE,
        .major_ver = 0,
        .minor_ver = 0,
    };

    return cfg;
}

bool R_SupportsDebugLines(void)
{
    return VK_Debug_Supported();
}

void R_AddDebugText_(const vec3_t origin, const vec3_t angles, const char *text, float size,
                     color_t color, uint32_t time, bool depth_test)
{
    VK_Debug_AddText(origin, angles, text, size, color, time, depth_test);
}
