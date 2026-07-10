from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_vulkan_bridge_readiness as bridge_readiness  # noqa: E402


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_valid_repo(repo_root: Path) -> None:
    write_text(
        repo_root / "meson.build",
        """
renderer_vk_rtx_src = [
  'src/renderer/dds.c',
  'src/renderer/rmlui_bridge.cpp',
  'src/renderer/view_setup.c',
]
renderer_vk_src = [
  'src/renderer/dds.c',
  'src/renderer/rmlui_bridge.cpp',
  'src/renderer/view_setup.c',
]
renderer_gl_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_GL']
renderer_vk_rtx_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_VKPT', '-DRENDERER_VULKAN', '-DRENDERER_VULKAN_RTX=1']
renderer_vk_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_VKPT', '-DRENDERER_VULKAN', '-DRENDERER_VULKAN_LEGACY=1']
renderer_gl_deps = renderer_deps
if rmlui_runtime
  renderer_gl_deps += rmlui_dep
  renderer_gl_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
  renderer_vk_rtx_deps += rmlui_dep
  renderer_vk_rtx_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
  renderer_vk_deps += rmlui_dep
  renderer_vk_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
endif
""",
    )
    write_text(
        repo_root / "inc/renderer/renderer.h",
        """
typedef enum {
    R_RENDERER_RMLUI_FAMILY_NONE,
    R_RENDERER_RMLUI_FAMILY_OPENGL,
    R_RENDERER_RMLUI_FAMILY_VULKAN,
    R_RENDERER_RMLUI_FAMILY_RTX_VKPT,
} renderer_rmlui_family_t;
renderer_rmlui_family_t R_RmlUiRendererFamily(void);
const char *R_RmlUiRendererName(void);
bool R_RmlUiCanRender(void);
void *R_RmlUiNativeRenderInterface(void);
typedef struct renderer_export_s {
    renderer_rmlui_family_t (*RmlUiRendererFamily)(void);
    const char *(*RmlUiRendererName)(void);
    bool (*RmlUiCanRender)(void);
    void *(*RmlUiNativeRenderInterface)(void);
} renderer_export_t;
""",
    )
    write_text(
        repo_root / "src/renderer/renderer_api.c",
        """
#if USE_REF != REF_GL
static bool Renderer_RmlUiCanRender(void)
{
    return false;
}

static void *Renderer_RmlUiNativeRenderInterface(void)
{
    return NULL;
}
#endif

static const renderer_export_t renderer_exports = {
    .RmlUiRendererFamily    = R_RmlUiRendererFamily,
    .RmlUiRendererName      = R_RmlUiRendererName,
#if USE_REF == REF_GL
    .RmlUiCanRender         = R_RmlUiCanRender,
    .RmlUiNativeRenderInterface = R_RmlUiNativeRenderInterface,
#else
    .RmlUiCanRender         = Renderer_RmlUiCanRender,
    .RmlUiNativeRenderInterface = Renderer_RmlUiNativeRenderInterface,
#endif
};
""",
    )
    write_text(
        repo_root / "src/renderer/rmlui_bridge.cpp",
        """
#if UI_RML_HAS_RUNTIME
#include <RmlUi/Core/RenderInterface.h>
class R_RmlUiOpenGLRenderInterface final : public Rml::RenderInterface {};
static R_RmlUiOpenGLRenderInterface r_rmlui_opengl_render_interface;
#endif

#if UI_RML_HAS_RUNTIME && defined(RENDERER_VULKAN_LEGACY)
class R_RmlUiVulkanRenderInterface final : public Rml::RenderInterface {};
#endif

#if UI_RML_HAS_RUNTIME && defined(RENDERER_VULKAN_RTX)
class R_RmlUiRtxVkptRenderInterface final : public Rml::RenderInterface {};
#endif

renderer_rmlui_family_t R_RmlUiRendererFamily(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return R_RENDERER_RMLUI_FAMILY_OPENGL;
#elif defined(RENDERER_VULKAN_LEGACY)
    return R_RENDERER_RMLUI_FAMILY_VULKAN;
#elif defined(RENDERER_VULKAN_RTX)
    return R_RENDERER_RMLUI_FAMILY_RTX_VKPT;
#else
    return R_RENDERER_RMLUI_FAMILY_NONE;
#endif
}

const char *R_RmlUiRendererName(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return "OpenGL RmlUi render-interface primitives";
#elif defined(RENDERER_VULKAN_LEGACY)
    return "Vulkan RmlUi render-interface inactive";
#elif defined(RENDERER_VULKAN_RTX)
    return "RTX/vkpt RmlUi render-interface inactive";
#else
    return "none";
#endif
}
""",
    )
    write_text(
        repo_root / "src/client/renderer.cpp",
        """
static ui_rml_renderer_family_t RmlUi_RendererFamilyFromRenderer(renderer_rmlui_family_t family)
{
    switch (family) {
    case R_RENDERER_RMLUI_FAMILY_OPENGL:
        return UI_RML_RENDERER_FAMILY_OPENGL;
    case R_RENDERER_RMLUI_FAMILY_VULKAN:
        return UI_RML_RENDERER_FAMILY_VULKAN;
    case R_RENDERER_RMLUI_FAMILY_RTX_VKPT:
        return UI_RML_RENDERER_FAMILY_RTX_VKPT;
    default:
        return UI_RML_RENDERER_FAMILY_NONE;
    }
}
""",
    )
    write_text(
        repo_root / "src/rend_vk/vk_ui.h",
        """
void VK_UI_BeginFrame(void);
void VK_UI_EndFrame(void);
void VK_UI_Record(VkCommandBuffer cmd, const VkExtent2D *extent);
void VK_UI_SetClipRect(const clipRect_t *clip);
qhandle_t VK_UI_RegisterRawImage(const char *name, int width, int height, byte *pic,
                                 imagetype_t type, imageflags_t flags);
VkDescriptorSet VK_UI_GetDescriptorSetForImage(qhandle_t pic);
bool VK_UI_UpdateImageRGBA(qhandle_t handle, int width, int height, const byte *pic);
bool VK_UI_UpdateImageRGBASubRect(qhandle_t handle, int x, int y, int width, int height, const byte *pic);
void VK_UI_DrawPic(int x, int y, color_t color, qhandle_t pic);
void VK_UI_DrawStretchPic(int x, int y, int w, int h, color_t color, qhandle_t pic);
void VK_UI_DrawStretchSubPic(int x, int y, int w, int h, float s1, float t1, float s2, float t2, color_t color, qhandle_t pic);
""",
    )
    write_text(
        repo_root / "src/rend_vk/vk_ui.c",
        """
void VK_UI_BeginFrame(void) {}
void VK_UI_EndFrame(void) {}
void VK_UI_Record(VkCommandBuffer cmd, const VkExtent2D *extent)
{
    vkCmdSetScissor(cmd, 0, 1, &draw->scissor);
}
void VK_UI_SetClipRect(const clipRect_t *clip) {}
qhandle_t VK_UI_RegisterRawImage(const char *name, int width, int height, byte *pic, imagetype_t type, imageflags_t flags) { return 1; }
VkDescriptorSet VK_UI_GetDescriptorSetForImage(qhandle_t pic) { return VK_NULL_HANDLE; }
bool VK_UI_UpdateImageRGBA(qhandle_t handle, int width, int height, const byte *pic) { return true; }
bool VK_UI_UpdateImageRGBASubRect(qhandle_t handle, int x, int y, int width, int height, const byte *pic) { return true; }
void VK_UI_DrawPic(int x, int y, color_t color, qhandle_t pic) {}
void VK_UI_DrawStretchPic(int x, int y, int w, int h, color_t color, qhandle_t pic) {}
void VK_UI_DrawStretchSubPic(int x, int y, int w, int h, float s1, float t1, float s2, float t2, color_t color, qhandle_t pic) {}
static VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
""",
    )
    write_text(
        repo_root / "src/rend_vk/vk_main.c",
        """
void R_BeginFrame(void) { VK_UI_BeginFrame(); }
void R_EndFrame(void) { VK_UI_EndFrame(); }
void R_Record(void) { VK_UI_Record(cmd, &ctx->swapchain.extent); }
qhandle_t R_RegisterRawImage(const char *name, int width, int height, byte *pic, imagetype_t type, imageflags_t flags)
{
    return VK_UI_RegisterRawImage(name, width, height, pic, type, flags);
}
void R_SetClipRect(const clipRect_t *clip) { VK_UI_SetClipRect(clip); }
void R_DrawPic(int x, int y, color_t color, qhandle_t pic) { VK_UI_DrawPic(x, y, color, pic); }
void R_DrawStretchPic(int x, int y, int w, int h, color_t color, qhandle_t pic)
{
    VK_UI_DrawStretchPic(x, y, w, h, color, pic);
}
bool R_UpdateImageRGBA(qhandle_t handle, int width, int height, const byte *pic)
{
    return VK_UI_UpdateImageRGBA(handle, width, height, pic);
}
""",
    )
    write_text(
        repo_root / "src/rend_rtx/vkpt/vkpt.h",
        """
VkResult vkpt_draw_submit_stretch_pics(VkCommandBuffer cmd_buf);
VkResult vkpt_draw_clear_stretch_pics(void);
void R_SetClipRect(const clipRect_t *clip);
void R_DrawPic(int x, int y, color_t color, qhandle_t pic);
void R_DrawStretchPic(int x, int y, int w, int h, color_t color, qhandle_t pic);
void IMG_Load_RTX(image_t *image, byte *pic);
bool R_UpdateImageRGBA(qhandle_t handle, int width, int height, const byte *pic);
""",
    )
    write_text(
        repo_root / "src/rend_rtx/vkpt/draw.c",
        """
static clipRect_t clip_rect;
static bool clip_enable = false;
static inline void enqueue_stretch_pic(int x, int y, int w, int h, float s1, float t1, float s2, float t2, uint32_t color, qhandle_t pic) {}
VkResult vkpt_draw_clear_stretch_pics(void) { return VK_SUCCESS; }
VkResult vkpt_draw_submit_stretch_pics(VkCommandBuffer cmd_buf) { return VK_SUCCESS; }
void R_SetClipRect(const clipRect_t *clip) { clip_enable = clip != NULL; clip_rect.left = 0; }
void R_DrawStretchPic(int x, int y, int w, int h, color_t color, qhandle_t pic)
{
    enqueue_stretch_pic(x, y, w, h, 0, 0, 1, 1, color.u32, pic);
}
void R_DrawPic(int x, int y, color_t color, qhandle_t pic) { R_DrawStretchPic(x, y, 1, 1, color, pic); }
""",
    )
    write_text(
        repo_root / "src/rend_rtx/vkpt/main.c",
        """
void frame(void)
{
    vkpt_draw_clear_stretch_pics();
    vkpt_textures_update_descriptor_set();
    vkpt_draw_submit_stretch_pics(cmd_buf);
    IMG_Load = IMG_Load_RTX;
}
""",
    )
    write_text(
        repo_root / "src/rend_rtx/refresh/images.c",
        """
qhandle_t R_RegisterImage(const char *name, imagetype_t type, imageflags_t flags) { return 1; }
qhandle_t R_RegisterRawImage(const char *name, int width, int height, byte* pic, imagetype_t type, imageflags_t flags) { return 1; }
""",
    )
    write_text(
        repo_root / "src/rend_rtx/vkpt/textures.c",
        """
VkDescriptorSet qvk_get_current_desc_set_textures(void) { return qvk.desc_set_textures_even; }
void vkpt_textures_update_descriptor_set(void) {}
void IMG_Load_RTX(image_t *image, byte *pic) {}
bool R_UpdateImageRGBA(qhandle_t handle, int width, int height, const byte *pic) { return true; }
""",
    )
    write_text(
        repo_root / "src/rend_rtx/vkpt/shader/stretch_pic.vert",
        """
struct StretchPic { uint color, tex_handle; };
layout(set = 0, binding = 0) readonly buffer SBO { StretchPic stretch_pics[]; };
void main() { StretchPic sp = stretch_pics[gl_InstanceIndex]; }
""",
    )
    write_text(
        repo_root / "src/rend_rtx/vkpt/shader/stretch_pic.frag",
        """
layout(location = 0) out vec4 outColor;
void main() { outColor = global_textureLod(0, vec2(0), 0); }
""",
    )


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    result = bridge_readiness.main(
        [
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_valid_bridge_readiness_audit_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)

    result, captured = run_checker(repo_root, capsys)

    assert result == 0
    assert "Malformed findings: 0" in captured.out
    assert "Vulkan (blocked_until_native): pass" in captured.out
    assert "RTX/vkpt (blocked_until_native): pass" in captured.out
    assert "foundation_ok: yes" in captured.out
    assert "native_bridge_claimed: no" in captured.out
    assert "activation_status: partial_activation_blocked" in captured.out
    assert "next_activation_requirement: native_interface_export_present" in captured.out
    assert "Result: RmlUi Vulkan/RTX bridge-readiness audit passed." in captured.out


def test_json_report_exposes_foundations_and_missing_bridge_requirements(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["counts"]["lanes"] == 2
    assert payload["counts"]["foundation_lanes"] == 2
    assert payload["counts"]["native_bridge_lanes"] == 0
    assert payload["counts"]["blocked_lanes"] == 2
    assert payload["counts"]["activation_complete_lanes"] == 0
    assert payload["counts"]["partial_activation_lanes"] == 2
    assert payload["counts"]["inactive_activation_lanes"] == 0
    assert payload["counts"]["activation_requirements"] == 10
    assert payload["counts"]["satisfied_activation_requirements"] == 8
    assert payload["counts"]["pending_activation_requirements"] == 2
    assert payload["counts"]["missing_bridge_requirements"] == 2
    assert payload["lanes"]["vulkan"]["foundations"]["draw_entrypoints_present"] is True
    assert payload["lanes"]["vulkan"]["guardrails"]["runtime_dependency_inactive"] is True
    assert payload["lanes"]["vulkan"]["activation_requirements"] == {
        "native_bridge_class_present": True,
        "native_bridge_source_compiled": True,
        "native_family_export_present": True,
        "native_interface_export_present": False,
        "runtime_dependency_enabled": True,
    }
    assert payload["lanes"]["vulkan"]["activation_status"] == "partial_activation_blocked"
    assert payload["lanes"]["vulkan"]["activation_complete"] is False
    assert payload["lanes"]["vulkan"]["satisfied_activation_requirement_ids"] == [
        "native_bridge_class_present",
        "native_bridge_source_compiled",
        "native_family_export_present",
        "runtime_dependency_enabled",
    ]
    assert payload["lanes"]["vulkan"]["pending_activation_requirement_ids"] == [
        "native_interface_export_present",
    ]
    assert payload["lanes"]["vulkan"]["next_activation_requirement"] == "native_interface_export_present"
    assert payload["lanes"]["rtx_vkpt"]["foundations"]["stretch_pic_submit_present"] is True
    assert payload["lanes"]["rtx_vkpt"]["native_bridge_claimed"] is False


def test_vulkan_partial_runtime_dependency_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    meson_path = repo_root / "meson.build"
    meson_path.write_text(
        meson_path.read_text(encoding="utf-8").replace(
            "  renderer_vk_deps += rmlui_dep\n",
            "",
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "vulkan: Vulkan RmlUi runtime dependency must stay inactive until a native interface export exists" in captured.out


def test_vulkan_family_export_without_bridge_class_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    bridge_path = repo_root / "src/renderer/rmlui_bridge.cpp"
    bridge_path.write_text(
        bridge_path.read_text(encoding="utf-8").replace(
            "R_RmlUiVulkanRenderInterface",
            "R_RmlUiNativeVulkanBridgeMissing",
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert (
        "vulkan: Vulkan RmlUi family export must stay inactive until bridge source, runtime, and interface exports are wired"
        in captured.out
    )


def test_partial_vulkan_bridge_class_claim_is_accepted_as_incomplete_activation(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["counts"]["activation_requirements"] == 10
    assert payload["counts"]["activation_complete_lanes"] == 0
    assert payload["counts"]["partial_activation_lanes"] == 2
    assert payload["counts"]["inactive_activation_lanes"] == 0
    assert payload["counts"]["satisfied_activation_requirements"] == 8
    assert payload["counts"]["pending_activation_requirements"] == 2
    assert payload["counts"]["missing_bridge_requirements"] == 2
    assert payload["lanes"]["vulkan"]["activation_status"] == "partial_activation_blocked"
    assert payload["lanes"]["vulkan"]["activation_complete"] is False
    assert payload["lanes"]["vulkan"]["activation_requirements"]["native_bridge_class_present"] is True
    assert payload["lanes"]["vulkan"]["satisfied_activation_requirement_ids"] == [
        "native_bridge_class_present",
        "native_bridge_source_compiled",
        "native_family_export_present",
        "runtime_dependency_enabled",
    ]
    assert payload["lanes"]["vulkan"]["pending_activation_requirement_ids"] == [
        "native_interface_export_present",
    ]
    assert payload["lanes"]["vulkan"]["next_activation_requirement"] == "native_interface_export_present"
    assert payload["lanes"]["vulkan"]["guardrails"]["native_bridge_class_inactive"] is True
    assert payload["lanes"]["vulkan"]["guardrails"]["native_family_export_inactive"] is True
    assert payload["lanes"]["vulkan"]["guardrails"]["runtime_dependency_inactive"] is True
    assert payload["lanes"]["vulkan"]["native_bridge_claimed"] is False
    assert payload["errors"] == []


def test_vulkan_lane_must_not_redirect_to_opengl(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    client_path = repo_root / "src/client/renderer.cpp"
    client_path.write_text(
        client_path.read_text(encoding="utf-8").replace(
            "case R_RENDERER_RMLUI_FAMILY_VULKAN:\n        return UI_RML_RENDERER_FAMILY_VULKAN;",
            "case R_RENDERER_RMLUI_FAMILY_VULKAN:\n        return UI_RML_RENDERER_FAMILY_OPENGL;",
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "Vulkan/RTX RmlUi lanes must not map to OpenGL" in captured.out
    assert "vulkan: client renderer must map the Vulkan family to the Vulkan UI lane" in captured.out


def test_vulkan_foundation_primitives_are_required(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    vk_main_path = repo_root / "src/rend_vk/vk_main.c"
    vk_main_path.write_text(
        vk_main_path.read_text(encoding="utf-8").replace(
            "R_DrawStretchPic",
            "R_DrawStretchPicMissing",
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert (
        "vulkan: native Vulkan UI draw entrypoints must remain available for a future RmlUi bridge"
        in captured.out
    )
