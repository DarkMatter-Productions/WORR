from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_renderer_matrix as renderer_matrix  # noqa: E402


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_valid_repo(repo_root: Path) -> None:
    write_text(
        repo_root / "meson.build",
        """
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
#include "../rend_gl/gl.h"
#include <RmlUi/Core/RenderInterface.h>
class R_RmlUiOpenGLRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return {}; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &) override { return {}; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return {}; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};
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

bool R_RmlUiCanRender(void)
{
#if UI_RML_HAS_RUNTIME
    return true;
#else
    return false;
#endif
}

void *R_RmlUiNativeRenderInterface(void)
{
#if UI_RML_HAS_RUNTIME
    return &r_rmlui_opengl_render_interface;
#else
    return NULL;
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

static void RmlUi_RegisterRendererBridge(void)
{
    ui_rml_renderer_interface_t renderer = {};
    renderer.family = RmlUi_RendererFamilyFromRenderer(RmlUi_RendererFamilyExport());
    if (renderer.family == UI_RML_RENDERER_FAMILY_NONE) {
        UI_Rml_ClearRendererInterface();
        return;
    }
    UI_Rml_SetRendererInterface(&renderer);
}

void CL_InitRenderer(void)
{
    RmlUi_RegisterRendererBridge();
}
""",
    )
    write_text(
        repo_root / "tools/ui_smoke/check_rmlui_runtime_capture.py",
        """
DEFAULT_ROUTE_MATRIX = ("main", "game", "download_status")

def build_engine_command():
    command = [
        "+set",
        "r_renderer",
        "opengl",
    ]
    return command
""",
    )


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    result = renderer_matrix.main(
        [
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_valid_renderer_matrix_guardrail_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)

    result, captured = run_checker(repo_root, capsys)

    assert result == 0
    assert "Malformed findings: 0" in captured.out
    assert "OpenGL (native_guarded): pass" in captured.out
    assert "Vulkan (blocked_until_native): pass" in captured.out
    assert "RTX/vkpt (blocked_until_native): pass" in captured.out
    assert "capture_harness_uses_opengl: yes" in captured.out
    assert "Result: RmlUi renderer-family matrix guardrail passed." in captured.out


def test_json_report_exposes_renderer_lanes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["contract"]["renderer_families_declared"] is True
    assert payload["contract"]["no_vulkan_or_rtx_to_opengl_redirect"] is True
    assert payload["lanes"]["opengl"]["expected_status"] == "native_guarded"
    assert payload["lanes"]["opengl"]["facts"]["native_render_interface_returned"] is True
    assert payload["lanes"]["vulkan"]["expected_status"] == "blocked_until_native"
    assert payload["lanes"]["vulkan"]["facts"]["renderer_api_non_gl_unavailable"] is True
    assert payload["lanes"]["vulkan"]["facts"]["native_family_export_inactive"] is True
    assert payload["lanes"]["rtx_vkpt"]["expected_status"] == "blocked_until_native"
    assert payload["lanes"]["rtx_vkpt"]["facts"]["runtime_dependency_inactive"] is True
    assert payload["counts"]["native_guarded_lanes"] == 1
    assert payload["counts"]["blocked_lanes"] == 2
    assert payload["counts"]["errors"] == 0


def test_opengl_lane_must_report_can_render_true(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    bridge_path = repo_root / "src/renderer/rmlui_bridge.cpp"
    bridge_path.write_text(
        bridge_path.read_text(encoding="utf-8").replace(
            "return true;",
            "return false;",
            1,
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "opengl: OpenGL native bridge must report CanRender=true" in captured.out


def test_vulkan_lane_must_not_map_to_opengl(
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
    assert (
        "vulkan: Vulkan runtime dependency must stay inactive until native rendering is available"
        in captured.out
    )


def test_capture_harness_must_stay_explicitly_opengl_scoped(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    capture_path = repo_root / "tools/ui_smoke/check_rmlui_runtime_capture.py"
    capture_path.write_text(
        capture_path.read_text(encoding="utf-8").replace(
            '"opengl"',
            '"vulkan"',
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "guarded runtime capture matrix must remain explicitly OpenGL-scoped" in captured.out
