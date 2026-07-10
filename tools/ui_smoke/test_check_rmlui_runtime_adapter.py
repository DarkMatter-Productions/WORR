from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_runtime_adapter as runtime_adapter  # noqa: E402


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_valid_repo(repo_root: Path, *, adapter_text: str | None = None) -> None:
    write_text(
        repo_root / "meson.build",
        """
client_src = [
  'src/client/ui_rml/ui_rml.cpp',
  'src/client/ui_rml/ui_rml_runtime.cpp',
]
renderer_src = [
  'src/renderer/rmlui_bridge.cpp',
]
renderer_vk_rtx_src = [
  'src/renderer/rmlui_bridge.cpp',
]
renderer_vk_src = [
  'src/renderer/rmlui_bridge.cpp',
]
renderer_cpp_args = [
  '-DHAVE_CONFIG_H',
  '-DQ2PROTO_CONFIG_H="common/q2proto_config.h"',
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
rmlui_cmake_options = {
  'RMLUI_FONT_ENGINE': 'none',
  'RMLUI_SAMPLES': false,
  'RMLUI_TESTS': false,
}
""",
    )
    write_text(
        repo_root / "src/client/ui_rml/ui_rml.h",
        """
#define UI_RML_HAS_RUNTIME 0
typedef enum {
    UI_RML_AVAILABILITY_RENDERER_UNAVAILABLE,
} ui_rml_availability_t;
typedef struct {
    bool (*OpenRoute)(const char *, const char *);
    void (*CloseRoute)(void);
    bool (*Update)(int width, int height, unsigned realtime);
    bool (*Render)(void);
    bool (*KeyEvent)(int key, bool down);
    bool (*CharEvent)(int key);
    bool (*MouseEvent)(int x, int y);
    bool (*CanOpenRoutes)(void);
    bool (*ProbeRoute)(const char *, const char *);
} ui_rml_runtime_interface_t;
typedef enum {
    UI_RML_RENDERER_FAMILY_NONE,
    UI_RML_RENDERER_FAMILY_OPENGL,
    UI_RML_RENDERER_FAMILY_VULKAN,
    UI_RML_RENDERER_FAMILY_RTX_VKPT,
} ui_rml_renderer_family_t;
typedef struct {
    ui_rml_renderer_family_t family;
    const char *(*RendererName)(void);
    bool (*CanRender)(void);
    void *(*NativeRenderInterface)(void);
} ui_rml_renderer_interface_t;
void UI_Rml_RegisterCompiledRuntime(void);
void UI_Rml_SetRuntimeInterface(const ui_rml_runtime_interface_t *runtime);
void UI_Rml_SetRendererInterface(const ui_rml_renderer_interface_t *renderer);
void UI_Rml_ClearRendererInterface(void);
bool UI_Rml_RendererIsAvailable(void);
bool UI_Rml_IsRouteActive(void);
bool UI_Rml_Draw(unsigned realtime);
bool UI_Rml_KeyEvent(int key, bool down);
bool UI_Rml_CharEvent(int key);
bool UI_Rml_MouseEvent(int x, int y);
void UI_Rml_CloseActiveRoute(void);
""",
    )
    write_text(
        repo_root / "src/client/ui_rml/ui_rml.cpp",
        """
static ui_rml_runtime_interface_t ui_rml_runtime;
static bool ui_rml_route_active;
static char ui_rml_active_route[MAX_QPATH];
static int ui_rml_route_metrics;
static ui_rml_renderer_interface_t ui_rml_renderer;
static bool ui_rml_renderer_registered;
static const char *ui_rml_runtime_menu_routes = "main game download_status";
static bool UI_Rml_FindRuntimeMenuRoute(const char *route_id)
{
    return route_id && (!strcmp(route_id, "main") || !strcmp(route_id, "game") || !strcmp(route_id, "download_status"));
}
static bool UI_Rml_RuntimeRouteIsAllowed(const char *route_id)
{
    return route_id && (!strcmp(route_id, "core.runtime_smoke") || UI_Rml_FindRuntimeMenuRoute(route_id));
}
void UI_Rml_Init(void)
{
#if UI_RML_HAS_RUNTIME
    UI_Rml_RegisterCompiledRuntime();
#endif
}
const ui_rml_renderer_interface_t *UI_Rml_RendererInterface(void)
{
    return ui_rml_renderer_registered ? &ui_rml_renderer : NULL;
}
const char *UI_Rml_RendererFamilyString(ui_rml_renderer_family_t family)
{
    switch (family) {
    case UI_RML_RENDERER_FAMILY_OPENGL:
        return "opengl";
    case UI_RML_RENDERER_FAMILY_VULKAN:
        return "vulkan";
    case UI_RML_RENDERER_FAMILY_RTX_VKPT:
        return "rtx_vkpt";
    default:
        return "none";
    }
}
const char *UI_Rml_RendererName(void)
{
    return "test";
}
bool UI_Rml_RendererIsAvailable(void)
{
    return ui_rml_renderer_registered &&
        ui_rml_renderer.CanRender &&
        ui_rml_renderer.NativeRenderInterface &&
        ui_rml_renderer.CanRender() &&
        ui_rml_renderer.NativeRenderInterface() != NULL;
}
static bool UI_Rml_RuntimeCanOpenRoutes(void)
{
    if (!UI_Rml_RendererIsAvailable()) {
        return false;
    }
    if (ui_rml_runtime.CanOpenRoutes) {
        return ui_rml_runtime.CanOpenRoutes();
    }
    return true;
}
static void UI_Rml_StopRuntime(void)
{
}
static void UI_Rml_ClearActiveRoute(bool notify_runtime)
{
    if (notify_runtime) {
        UI_Rml_ClearActiveRoute(true);
    }
    Key_SetDest((keydest_t)(Key_GetDest() | KEY_MENU));
}
bool UI_Rml_KeyEvent(int key, bool down)
{
    if (ui_rml_runtime.KeyEvent) {
        ui_rml_runtime.KeyEvent(key, down);
    }
    if (down && (key == K_MOUSE2 || key == K_MWHEELUP)) {
        ui_rml_route_metrics++;
    }
    return true;
}
bool UI_Rml_CharEvent(int key)
{
    if (ui_rml_runtime.CharEvent) {
        ui_rml_runtime.CharEvent(key);
    }
    return true;
}
bool UI_Rml_MouseEvent(int x, int y)
{
    if (ui_rml_runtime.MouseEvent) {
        ui_rml_runtime.MouseEvent(x, y);
    }
    return true;
}
static bool UI_Rml_OpenRouteInternal(const char *route)
{
    int width = 640;
    int height = 480;
    unsigned realtime = 0;
    if (UI_Rml_RuntimeRouteIsAllowed(route) && ui_rml_runtime.Update(width, height, realtime)) {
        return ui_rml_runtime.Render();
    }
    return false;
}
static void UI_Rml_RuntimeOpenRoute_f(void)
{
    UI_Rml_OpenRouteInternal("core.runtime_smoke");
}
static void UI_Rml_RuntimeCloseRoute_f(void)
{
    UI_Rml_ClearActiveRoute(true);
}
static void UI_Rml_PrintRuntimeStatus(void)
{
    Com_Printf("updates=%u renders=%u", 1, 1);
    Com_Printf("opens=%u closes=%u close_requests=%u synthetic_inputs=%u", 1, 1, 1, 1);
    Com_Printf("mouse_moves=%u", 1);
}
static void UI_Rml_RuntimeStatus_f(void)
{
    UI_Rml_PrintRuntimeStatus();
}
static void UI_Rml_RuntimeSyntheticInput_f(void)
{
    ui_rml_route_metrics++;
    UI_Rml_MouseEvent(128, 192);
    UI_Rml_CharEvent('w');
    UI_Rml_KeyEvent(K_MWHEELUP, true);
    UI_Rml_KeyEvent(K_MOUSE2, true);
}
static void UI_Rml_RuntimeCapture_f(void)
{
    UI_Rml_PrintRuntimeStatus();
}
static void UI_Rml_RuntimeCaptureMenu_f(void)
{
    UI_Rml_FindRuntimeMenuRoute("main");
    UI_Rml_PrintRuntimeStatus();
}
const char *UI_Rml_AvailabilityString(void)
{
    return "renderer_unavailable";
}
static void UI_Rml_RuntimeProbeRoute_f(void)
{
}
static const char *ui_rml_runtime_probe = "ui_rml_runtime_probe";
static const char *ui_rml_runtime_open = "ui_rml_runtime_open";
static const char *ui_rml_runtime_close = "ui_rml_runtime_close";
static const char *ui_rml_runtime_status = "ui_rml_runtime_status";
static const char *ui_rml_runtime_capture = "ui_rml_runtime_capture";
static const char *ui_rml_runtime_capture_menu = "ui_rml_runtime_capture_menu";
static const char *ui_rml_runtime_synthetic_input = "ui_rml_runtime_synthetic_input";
""",
    )
    write_text(
        repo_root / "src/client/ui_rml/ui_rml_runtime.cpp",
        adapter_text
        if adapter_text is not None
        else """
#include "ui_rml.h"
#if UI_RML_HAS_RUNTIME
#undef DotProduct
#undef CrossProduct
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
static Rml::Context *ui_rml_context;
static Rml::ElementDocument *ui_rml_document;
static const char *ui_rml_q2r_font_source_marker = "loaded from Quake II Rerelease font";
static const char *ui_rml_q2r_font_source_prefix = "Quake II Rerelease:";
static const char *ui_rml_q2r_display_font = "fonts/RussoOne-Regular.ttf";
static const char *ui_rml_q2r_ui_font = "fonts/Montserrat-Regular.ttf";
static const char *ui_rml_q2r_mono_font = "fonts/RobotoMono-Regular.ttf";
class UI_Rml_SmokeFontEngineInterface final : public Rml::FontEngineInterface {
    Rml::FontMetrics metrics;
    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String &, Rml::Style::FontStyle, Rml::Style::FontWeight, int) override { return 1; }
    const Rml::FontMetrics &GetFontMetrics(Rml::FontFaceHandle) override { return metrics; }
    int GetStringWidth(Rml::FontFaceHandle, Rml::StringView string, const Rml::TextShapingContext &, Rml::Character) override { return static_cast<int>(string.size()); }
    int GenerateString(Rml::RenderManager &, Rml::FontFaceHandle, Rml::FontEffectsHandle, Rml::StringView string, Rml::Vector2f, Rml::ColourbPremultiplied, float, const Rml::TextShapingContext &, Rml::TexturedMeshList &mesh_list) override {
        Rml::TexturedMesh text_mesh;
        mesh_list.push_back(text_mesh);
        Com_Printf("RmlUi smoke font engine generated glyph geometry");
        return static_cast<int>(string.size());
    }
};
static UI_Rml_SmokeFontEngineInterface ui_rml_font_interface;
static void UI_Rml_InstallCoreInterfaces(void)
{
    const ui_rml_renderer_interface_t *renderer = UI_Rml_RendererInterface();
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    Rml::SetFontEngineInterface(&ui_rml_font_interface);
    if (renderer && renderer->NativeRenderInterface) {
        void *native_render_interface = renderer->NativeRenderInterface();
        if (native_render_interface) {
            Rml::SetRenderInterface(static_cast<Rml::RenderInterface *>(native_render_interface));
        }
    }
}
static void file_api(void)
{
    FS_OpenFile(path, &handle, FS_MODE_READ);
    FS_Read(buffer, size, handle);
    FS_Seek(handle, 0, SEEK_SET);
    FS_Tell(handle);
    FS_Length(handle);
    FS_CloseFile(handle);
}
static void system_api(void)
{
    Sys_Milliseconds();
    Com_WPrintf("warning");
    Com_Printf("info");
}
static bool UI_Rml_CompiledRuntimeCanOpenRoutes(void)
{
    return UI_Rml_RendererIsAvailable();
}
static bool UI_Rml_CompiledRuntimeRouteIsAllowed(const char *route_id)
{
    return route_id && (!strcmp(route_id, "core.runtime_smoke") || !strcmp(route_id, "main") || !strcmp(route_id, "game") || !strcmp(route_id, "download_status"));
}
static const char *UI_Rml_CompiledRuntimeName(void)
{
    Rml::GetVersion();
    return "RmlUi";
}
static bool UI_Rml_CompiledRuntimeInit(void)
{
    UI_Rml_InstallCoreInterfaces();
    return Rml::Initialise();
}
static bool UI_Rml_CompiledRuntimeProbeRoute(const char *, const char *document_path)
{
    Rml::String contents;
    return Rml::GetFileInterface()->LoadFile(document_path, contents);
}
static void UI_Rml_CompiledRuntimeShutdown(void)
{
    Rml::RemoveContext("worr_ui");
    Rml::Shutdown();
}
static bool UI_Rml_CompiledRuntimeOpenRoute(const char *route_id, const char *document_path)
{
    if (!UI_Rml_CompiledRuntimeRouteIsAllowed(route_id)) return false;
    if (UI_Rml_RendererFamily() != UI_RML_RENDERER_FAMILY_OPENGL) return false;
    ui_rml_context = Rml::CreateContext("worr_ui", Rml::Vector2i(640, 480));
    ui_rml_document = ui_rml_context->LoadDocument(document_path);
    ui_rml_document->Show();
    ui_rml_context->Update();
    return true;
}
static void UI_Rml_CompiledRuntimeCloseRoute(void)
{
    ui_rml_document->Close();
    ui_rml_context->UnloadAllDocuments();
    ui_rml_context->Update();
}
static bool UI_Rml_CompiledRuntimeUpdate(int width, int height, unsigned realtime)
{
    (void)realtime;
    ui_rml_context->SetDimensions(Rml::Vector2i(width, height));
    ui_rml_context->Update();
    return true;
}
static bool UI_Rml_CompiledRuntimeRender(void)
{
    return ui_rml_context->Render();
}
static int UI_Rml_CompiledRuntimeModifiers(void)
{
    return Rml::Input::KM_CTRL;
}
static Rml::Input::KeyIdentifier UI_Rml_CompiledRuntimeKeyIdentifier(int key)
{
    (void)key;
    return Rml::Input::KI_ESCAPE;
}
static int UI_Rml_CompiledRuntimeMouseButtonIndex(int key)
{
    (void)key;
    return 0;
}
static bool UI_Rml_CompiledRuntimeKeyEvent(int key, bool down)
{
    (void)key;
    if (down) {
        ui_rml_context->ProcessKeyDown(UI_Rml_CompiledRuntimeKeyIdentifier(key), UI_Rml_CompiledRuntimeModifiers());
        ui_rml_context->ProcessMouseButtonDown(UI_Rml_CompiledRuntimeMouseButtonIndex(key), UI_Rml_CompiledRuntimeModifiers());
        ui_rml_context->ProcessMouseWheel(Rml::Vector2f(0, 1), UI_Rml_CompiledRuntimeModifiers());
    } else {
        ui_rml_context->ProcessKeyUp(UI_Rml_CompiledRuntimeKeyIdentifier(key), UI_Rml_CompiledRuntimeModifiers());
        ui_rml_context->ProcessMouseButtonUp(UI_Rml_CompiledRuntimeMouseButtonIndex(key), UI_Rml_CompiledRuntimeModifiers());
    }
    return true;
}
static bool UI_Rml_CompiledRuntimeCharEvent(int key)
{
    ui_rml_context->ProcessTextInput(static_cast<Rml::Character>(key));
    return true;
}
static bool UI_Rml_CompiledRuntimeMouseEvent(int x, int y)
{
    ui_rml_context->ProcessMouseMove(x, y, UI_Rml_CompiledRuntimeModifiers());
    return true;
}
void UI_Rml_RegisterCompiledRuntime(void)
{
    static const ui_rml_runtime_interface_t runtime = {
        UI_Rml_CompiledRuntimeOpenRoute,
        UI_Rml_CompiledRuntimeCloseRoute,
        UI_Rml_CompiledRuntimeUpdate,
        UI_Rml_CompiledRuntimeRender,
        UI_Rml_CompiledRuntimeKeyEvent,
        UI_Rml_CompiledRuntimeCharEvent,
        UI_Rml_CompiledRuntimeMouseEvent,
        UI_Rml_CompiledRuntimeCanOpenRoutes,
    };
    UI_Rml_SetRuntimeInterface(&runtime);
}
#endif
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
#include "../rend_gl/gl.h"
#if UI_RML_HAS_RUNTIME
#undef DotProduct
#undef CrossProduct
#include <unordered_map>
#include <vector>
#include <RmlUi/Core/RenderInterface.h>
class R_RmlUiOpenGLRenderInterface final : public Rml::RenderInterface {
    struct R_RmlUiCompiledGeometry {
        std::vector<glVertexDesc2D_t> vertices;
        std::vector<glIndex_t> indices;
    };
    struct R_RmlUiTexture {
        GLuint texnum;
        bool owned;
    };
    static std::unordered_map<Rml::TextureHandle, R_RmlUiTexture> r_rmlui_textures;
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int>) override {
        auto *compiled = new R_RmlUiCompiledGeometry;
        Rml::Colourb colour = vertices[0].colour.ToNonPremultiplied();
        compiled->vertices.push_back(glVertexDesc2D_t{});
        compiled->vertices[0].c = COLOR_U32_RGBA(colour.red, colour.green, colour.blue, colour.alpha);
        return reinterpret_cast<Rml::CompiledGeometryHandle>(compiled);
    }
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        auto *compiled = reinterpret_cast<R_RmlUiCompiledGeometry *>(geometry);
        auto *dst = reinterpret_cast<glVertexDesc2D_t *>(tess.vertices);
        GLuint texnum = TextureForHandle(texture);
        tess.texnum[TMU_TEXTURE] = texnum;
        tess.flags |= GLS_BLEND_BLEND | GLS_SHADE_SMOOTH;
        dst[0].xy[0] += translation.x;
        dst[0].xy[1] += translation.y;
        tess.indices[0] = compiled->indices[0];
        GL_Flush2D();
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override {
        auto *compiled = reinterpret_cast<R_RmlUiCompiledGeometry *>(geometry);
        delete compiled;
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &source) override {
        image_t *image = IMG_Find(source.c_str(), IT_PIC, IF_NONE);
        if (image == R_NOTEXTURE) return {};
        return RegisterTexture(image->texnum, false);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
        GLuint texnum = 0;
        qglGenTextures(1, &texnum);
        GL_ForceTexture(TMU_TEXTURE, texnum);
        qglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        UnpremultiplyTexture(bytes);
        c.texUploads++;
        return RegisterTexture(texnum, true);
    }
    void ReleaseTexture(Rml::TextureHandle texture) override {
        if (r_rmlui_textures[texture].owned) qglDeleteTextures(1, &r_rmlui_textures[texture].texnum);
    }
    void EnableScissorRegion(bool enable) override {
        GL_Flush2D();
        if (enable) qglEnable(GL_SCISSOR_TEST); else qglDisable(GL_SCISSOR_TEST);
        draw.scissor = enable;
    }
    void SetScissorRegion(Rml::Rectanglei region) override {
        qglScissor(region.Left(), r_config.height - region.Bottom(), region.Width(), region.Height());
        draw.scissor = true;
    }
    static void UnpremultiplyTexture(std::vector<Rml::byte> &) {}
    static Rml::TextureHandle RegisterTexture(GLuint, bool) { return 1; }
    static GLuint TextureForHandle(Rml::TextureHandle texture) { return texture ? 1 : TEXNUM_WHITE; }
};
static R_RmlUiOpenGLRenderInterface r_rmlui_opengl_render_interface;
#endif
renderer_rmlui_family_t R_RmlUiRendererFamily(void)
{
#if UI_RML_HAS_RUNTIME
    return R_RENDERER_RMLUI_FAMILY_OPENGL;
#else
    return R_RENDERER_RMLUI_FAMILY_NONE;
#endif
}
const char *R_RmlUiRendererName(void)
{
    return "OpenGL RmlUi render-interface primitives";
}
bool R_RmlUiCanRender(void)
{
    return true;
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
    renderer.family = RmlUi_RendererFamilyFromRenderer(R_RmlUiRendererFamily());
    renderer.RendererName = R_RmlUiRendererName;
    renderer.CanRender = R_RmlUiCanRender;
    renderer.NativeRenderInterface = R_RmlUiNativeRenderInterface;
    UI_Rml_SetRendererInterface(&renderer);
}
void CL_InitRenderer(void)
{
    cls.ref_initialized = true;
    RmlUi_RegisterRendererBridge();
}
void CL_ShutdownRenderer(void)
{
    UI_Shutdown();
    UI_Rml_ClearRendererInterface();
}
""",
    )
    write_text(
        repo_root / "src/client/ui_bridge.cpp",
        """
void UI_KeyEvent(int key, bool down)
{
    if (UI_Rml_KeyEvent(key, down)) return;
}
void UI_CharEvent(int key)
{
    if (UI_Rml_CharEvent(key)) return;
}
void UI_Draw(unsigned realtime)
{
    if (UI_Rml_Draw(realtime)) return;
    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->Draw) api->Draw(realtime);
}
void UI_Frame(int msec)
{
    if (UI_Rml_IsRouteActive()) return;
}
void UI_MouseEvent(int x, int y)
{
    if (UI_Rml_MouseEvent(x, y)) return;
}
""",
    )
    write_text(
        repo_root / "subprojects/rmlui.wrap",
        """
[wrap-file]
directory = RmlUi-6.2
method = cmake

[provide]
dependency_names = RmlUi, rmlui
""",
    )


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    result = runtime_adapter.main(
        [
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_valid_runtime_adapter_boundary_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)

    result, captured = run_checker(repo_root, capsys)

    assert result == 0
    assert "Malformed findings: 0" in captured.out
    assert "RmlUi Core symbols: Rml::GetVersion, Rml::Initialise, Rml::Shutdown" in captured.out
    assert "RmlUi interface symbols: Rml::SetSystemInterface, Rml::SetFileInterface, Rml::GetFileInterface" in captured.out
    assert "WORR filesystem symbols: FS_OpenFile, FS_CloseFile, FS_Read, FS_Seek, FS_Tell, FS_Length" in captured.out
    assert "Runtime file-probe hook: yes" in captured.out
    assert "Runtime context hooks declared: yes" in captured.out
    assert "Runtime context adapter: yes" in captured.out
    assert "Runtime active-route scaffold: yes" in captured.out
    assert "UI bridge draw hook: yes" in captured.out
    assert "Runtime open/close commands: yes" in captured.out
    assert "Runtime font engine adapter: yes" in captured.out
    assert "Runtime Q2R font candidates: yes" in captured.out
    assert "Runtime input hooks declared: yes" in captured.out
    assert "Runtime input adapter: yes" in captured.out
    assert "UI bridge input hook: yes" in captured.out
    assert "Runtime status/capture commands: yes" in captured.out
    assert "Renderer bridge listed in meson.build: yes" in captured.out
    assert "Renderer C++ args configured: yes" in captured.out
    assert "Renderer RmlUi runtime dependencies: yes" in captured.out
    assert "Renderer contract declared: yes" in captured.out
    assert "RmlUi render interface symbols: Rml::SetRenderInterface, Rml::RenderInterface" in captured.out
    assert "Renderer API contract declared: yes" in captured.out
    assert "Renderer API OpenGL only: yes" in captured.out
    assert "Client renderer bridge registered: yes" in captured.out
    assert "Client renderer family mapping: yes" in captured.out
    assert "OpenGL renderer bridge scaffolded: yes" in captured.out
    assert "OpenGL geometry cache: yes" in captured.out
    assert "OpenGL draw primitives: yes" in captured.out
    assert "OpenGL texture uploads: yes" in captured.out
    assert "OpenGL texture lifetime: yes" in captured.out
    assert "OpenGL scissor state: yes" in captured.out
    assert "OpenGL renderer CanRender true: yes" in captured.out
    assert "Renderer route gate: yes" in captured.out
    assert "Native render interface required: yes" in captured.out
    assert "Vulkan not redirected to OpenGL: yes" in captured.out
    assert "Route-open guard: yes" in captured.out
    assert "Provide dependencies: RmlUi, rmlui" in captured.out
    assert "Result: RmlUi runtime adapter boundary check passed." in captured.out


def test_json_report_exposes_adapter_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["adapter"]["listed_in_meson"] is True
    assert payload["adapter"]["renderer_bridge_listed_in_meson"] is True
    assert payload["adapter"]["renderer_bridge_listed_once_in_meson"] is False
    assert payload["adapter"]["renderer_bridge_meson_occurrences"] == 3
    assert payload["adapter"]["renderer_bridge_required_source_sets"] == [
        "renderer_src",
        "renderer_vk_rtx_src",
        "renderer_vk_src",
    ]
    assert payload["adapter"]["renderer_bridge_required_source_sets_present"] is True
    assert payload["adapter"]["renderer_cpp_args_configured"] is True
    assert payload["adapter"]["renderer_runtime_dependencies"] is True
    assert payload["adapter"]["runtime_guard_present"] is True
    assert payload["adapter"]["rmlui_core_include_guarded"] is True
    assert payload["adapter"]["rmlui_system_file_includes_guarded"] is True
    assert payload["adapter"]["rmlui_render_include_guarded"] is True
    assert payload["adapter"]["rmlui_macro_collision_guards"] is True
    assert payload["adapter"]["runtime_probe_hook_present"] is True
    assert payload["adapter"]["runtime_context_hooks_declared"] is True
    assert payload["adapter"]["runtime_context_adapter_present"] is True
    assert payload["adapter"]["runtime_context_scaffolded"] is True
    assert payload["adapter"]["runtime_draw_bridge_hooked"] is True
    assert payload["adapter"]["runtime_open_commands_present"] is True
    assert payload["adapter"]["runtime_font_engine_adapter_present"] is True
    assert payload["adapter"]["runtime_font_engine_q2r_fonts_present"] is True
    assert payload["adapter"]["runtime_input_hooks_declared"] is True
    assert payload["adapter"]["runtime_input_adapter_present"] is True
    assert payload["adapter"]["runtime_input_bridge_hooked"] is True
    assert payload["adapter"]["runtime_status_capture_commands_present"] is True
    assert payload["adapter"]["core_interfaces_installed_before_initialise"] is True
    assert payload["adapter"]["rmlui_render_interface_symbols"] == [
        "Rml::SetRenderInterface",
        "Rml::RenderInterface",
    ]
    assert payload["adapter"]["renderer_contract_declared"] is True
    assert payload["adapter"]["renderer_native_families"] == [
        "UI_RML_RENDERER_FAMILY_OPENGL",
        "UI_RML_RENDERER_FAMILY_VULKAN",
        "UI_RML_RENDERER_FAMILY_RTX_VKPT",
    ]
    assert payload["adapter"]["renderer_contract_scaffolded"] is True
    assert payload["adapter"]["renderer_api_contract_declared"] is True
    assert payload["adapter"]["renderer_api_exports_declared"] is True
    assert payload["adapter"]["renderer_api_opengl_only"] is True
    assert payload["adapter"]["renderer_api_non_gl_unavailable"] is True
    assert payload["adapter"]["client_renderer_bridge_registered"] is True
    assert payload["adapter"]["client_renderer_bridge_cleared"] is True
    assert payload["adapter"]["client_renderer_family_mapping"] is True
    assert payload["adapter"]["opengl_renderer_bridge_guarded"] is True
    assert payload["adapter"]["opengl_renderer_bridge_scaffolded"] is True
    assert payload["adapter"]["opengl_renderer_geometry_cache"] is True
    assert payload["adapter"]["opengl_renderer_draw_primitives"] is True
    assert payload["adapter"]["opengl_renderer_texture_uploads"] is True
    assert payload["adapter"]["opengl_renderer_texture_lifetime"] is True
    assert payload["adapter"]["opengl_renderer_scissor_state"] is True
    assert payload["adapter"]["opengl_renderer_bridge_family"] is True
    assert payload["adapter"]["opengl_renderer_can_render_true"] is True
    assert payload["adapter"]["renderer_route_gate_present"] is True
    assert payload["adapter"]["renderer_native_interface_required"] is True
    assert payload["adapter"]["vulkan_renderer_not_redirected"] is True
    assert payload["counts"]["worr_file_symbols"] == 6
    assert payload["counts"]["rmlui_render_interface_symbols"] == 2
    assert payload["counts"]["renderer_native_families"] == 3
    assert payload["adapter"]["route_open_guard_present"] is True
    assert payload["adapter"]["cmake_font_engine_none"] is True
    assert payload["adapter"]["cmake_samples_disabled"] is True
    assert payload["adapter"]["cmake_tests_disabled"] is True
    assert payload["wrap"]["provide_dependencies"] == ["RmlUi", "rmlui"]
    assert payload["errors"] == []


def test_missing_non_gl_renderer_bridge_source_set_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    meson_path = repo_root / "meson.build"
    meson_path.write_text(
        meson_path.read_text(encoding="utf-8").replace(
            """renderer_vk_src = [
  'src/renderer/rmlui_bridge.cpp',
]
""",
            "renderer_vk_src = []\n",
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert (
        "renderer bridge: source must be listed in renderer source sets: "
        "renderer_vk_src"
    ) in captured.out


def test_missing_adapter_meson_entry_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    write_text(
        repo_root / "meson.build",
        """
client_src = [
  'src/client/ui_rml/ui_rml.cpp',
]
rmlui_cmake_options = {
  'RMLUI_FONT_ENGINE': 'none',
  'RMLUI_SAMPLES': false,
  'RMLUI_TESTS': false,
}
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "adapter: source exists but is not listed in meson.build" in captured.out


def test_unguarded_rmlui_include_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(
        repo_root,
        adapter_text="""
#include "ui_rml.h"
#include <RmlUi/Core/Core.h>
#if UI_RML_HAS_RUNTIME
Rml::String version = Rml::GetVersion();
bool init = Rml::Initialise();
void shutdown(void) { Rml::Shutdown(); }
void UI_Rml_RegisterCompiledRuntime(void) { UI_Rml_SetRuntimeInterface(&runtime); }
#endif
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "RmlUi Core include is not behind UI_RML_HAS_RUNTIME" in captured.out


def test_missing_guarded_runtime_route_open_guard_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    adapter_path = repo_root / "src/client/ui_rml/ui_rml_runtime.cpp"
    adapter_path.write_text(
        adapter_path.read_text(encoding="utf-8").replace(
            '"core.runtime_smoke"',
            '"unguarded.route"',
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "missing guarded runtime-route open guard" in captured.out


def test_opengl_primitive_bridge_must_claim_render_ready(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    bridge_path = repo_root / "src/renderer/rmlui_bridge.cpp"
    bridge_path.write_text(
        bridge_path.read_text(encoding="utf-8").replace(
            "bool R_RmlUiCanRender(void)\n{\n    return true;\n}",
            "bool R_RmlUiCanRender(void)\n{\n    return false;\n}",
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "OpenGL primitive bridge must report CanRender true" in captured.out


def test_missing_core_interface_install_order_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(
        repo_root,
        adapter_text="""
#include "ui_rml.h"
#if UI_RML_HAS_RUNTIME
#undef DotProduct
#undef CrossProduct
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>
static bool UI_Rml_CompiledRuntimeCanOpenRoutes(void)
{
    return false;
}
static bool UI_Rml_CompiledRuntimeInit(void)
{
    bool ok = Rml::Initialise();
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    return ok;
}
static bool UI_Rml_CompiledRuntimeProbeRoute(const char *, const char *document_path)
{
    Rml::String contents;
    FS_OpenFile(path, &handle, FS_MODE_READ);
    FS_Read(buffer, size, handle);
    FS_Seek(handle, 0, SEEK_SET);
    FS_Tell(handle);
    FS_Length(handle);
    FS_CloseFile(handle);
    Sys_Milliseconds();
    Com_WPrintf("warning");
    Com_Printf("info");
    return Rml::GetFileInterface()->LoadFile(document_path, contents);
}
static const char *UI_Rml_CompiledRuntimeName(void) { Rml::GetVersion(); return "RmlUi"; }
static void UI_Rml_CompiledRuntimeShutdown(void) { Rml::Shutdown(); }
void UI_Rml_RegisterCompiledRuntime(void) { UI_Rml_SetRuntimeInterface(&runtime); }
#endif
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "system/file interfaces must be installed before Initialise" in captured.out


def test_missing_wrap_provide_dependency_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    write_text(
        repo_root / "subprojects/rmlui.wrap",
        """
[wrap-file]
directory = RmlUi-6.2
method = cmake
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "wrap: [provide] dependency_names must include rmlui" in captured.out
