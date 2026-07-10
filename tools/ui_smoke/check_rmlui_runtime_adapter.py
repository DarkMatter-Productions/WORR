#!/usr/bin/env python3
"""Validate the guarded compiled RmlUi runtime adapter boundary."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_MESON_BUILD = Path("meson.build")
DEFAULT_HEADER = Path("src/client/ui_rml/ui_rml.h")
DEFAULT_SCAFFOLD_SOURCE = Path("src/client/ui_rml/ui_rml.cpp")
DEFAULT_ADAPTER_SOURCE = Path("src/client/ui_rml/ui_rml_runtime.cpp")
DEFAULT_RENDERER_HEADER = Path("inc/renderer/renderer.h")
DEFAULT_RENDERER_API_SOURCE = Path("src/renderer/renderer_api.c")
DEFAULT_RENDERER_BRIDGE_SOURCE = Path("src/renderer/rmlui_bridge.cpp")
DEFAULT_CLIENT_RENDERER_SOURCE = Path("src/client/renderer.cpp")
DEFAULT_UI_BRIDGE_SOURCE = Path("src/client/ui_bridge.cpp")
DEFAULT_WRAP = Path("subprojects/rmlui.wrap")
PATH_SEP_RE = re.compile(r"[\\/]+")
GUARDED_RUNTIME_ROUTES = ("core.runtime_smoke", "main", "game", "download_status")
REQUIRED_RENDERER_BRIDGE_SOURCE_SETS = (
    "renderer_src",
    "renderer_vk_rtx_src",
    "renderer_vk_src",
)


@dataclass
class RuntimeAdapterReport:
    repo_root: Path
    meson_build_path: Path
    header_path: Path
    scaffold_source_path: Path
    adapter_source_path: Path
    renderer_header_path: Path
    renderer_api_source_path: Path
    renderer_bridge_source_path: Path
    client_renderer_source_path: Path
    ui_bridge_source_path: Path
    wrap_path: Path
    meson_build_exists: bool = False
    header_exists: bool = False
    scaffold_source_exists: bool = False
    adapter_source_exists: bool = False
    renderer_header_exists: bool = False
    renderer_api_source_exists: bool = False
    renderer_bridge_source_exists: bool = False
    client_renderer_source_exists: bool = False
    ui_bridge_source_exists: bool = False
    wrap_exists: bool = False
    adapter_listed_in_meson: bool = False
    renderer_bridge_listed_in_meson: bool = False
    renderer_bridge_listed_once_in_meson: bool = False
    renderer_bridge_meson_occurrences: int = 0
    renderer_bridge_required_source_sets: list[str] = field(default_factory=list)
    renderer_bridge_required_source_sets_present: bool = False
    renderer_cpp_args_configured: bool = False
    renderer_runtime_dependencies: bool = False
    adapter_runtime_guard_present: bool = False
    rmlui_core_include_guarded: bool = False
    rmlui_system_file_includes_guarded: bool = False
    rmlui_render_include_guarded: bool = False
    rmlui_macro_collision_guards: bool = False
    rmlui_core_symbols: list[str] = field(default_factory=list)
    rmlui_core_interface_symbols: list[str] = field(default_factory=list)
    rmlui_render_interface_symbols: list[str] = field(default_factory=list)
    runtime_font_engine_adapter_present: bool = False
    runtime_font_engine_q2r_fonts_present: bool = False
    worr_file_symbols: list[str] = field(default_factory=list)
    worr_system_symbols: list[str] = field(default_factory=list)
    runtime_registration_declared: bool = False
    runtime_registration_called: bool = False
    runtime_interface_registered: bool = False
    runtime_probe_hook_present: bool = False
    runtime_context_hooks_declared: bool = False
    runtime_context_adapter_present: bool = False
    runtime_context_scaffolded: bool = False
    runtime_draw_bridge_hooked: bool = False
    runtime_open_commands_present: bool = False
    runtime_input_hooks_declared: bool = False
    runtime_input_adapter_present: bool = False
    runtime_input_bridge_hooked: bool = False
    runtime_status_capture_commands_present: bool = False
    core_interfaces_installed_before_initialise: bool = False
    renderer_contract_declared: bool = False
    renderer_native_families: list[str] = field(default_factory=list)
    renderer_contract_scaffolded: bool = False
    renderer_api_contract_declared: bool = False
    renderer_api_exports_declared: bool = False
    renderer_api_opengl_only: bool = False
    renderer_api_non_gl_unavailable: bool = False
    client_renderer_bridge_registered: bool = False
    client_renderer_bridge_cleared: bool = False
    client_renderer_family_mapping: bool = False
    opengl_renderer_bridge_guarded: bool = False
    opengl_renderer_bridge_scaffolded: bool = False
    opengl_renderer_geometry_cache: bool = False
    opengl_renderer_draw_primitives: bool = False
    opengl_renderer_texture_uploads: bool = False
    opengl_renderer_texture_lifetime: bool = False
    opengl_renderer_scissor_state: bool = False
    opengl_renderer_bridge_family: bool = False
    opengl_renderer_can_render_true: bool = False
    renderer_route_gate_present: bool = False
    renderer_native_interface_required: bool = False
    vulkan_renderer_not_redirected: bool = False
    renderer_unavailable_state: bool = False
    route_open_guard_present: bool = False
    cmake_font_engine_none: bool = False
    cmake_samples_disabled: bool = False
    cmake_tests_disabled: bool = False
    wrap_provide_dependencies: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_path(path: Path, repo_root: Path) -> Path:
    if path.is_absolute():
        return path
    return repo_root / path


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve(strict=False).relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def normalize_repo_path(path: str) -> str:
    return PATH_SEP_RE.sub("/", path).strip("./")


def read_text_if_file(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8")


def source_listed(meson_build_text: str, source_path: Path, repo_root: Path) -> bool:
    candidates = [normalize_repo_path(source_path.as_posix())]
    try:
        candidates.append(
            normalize_repo_path(
                source_path.resolve(strict=False)
                .relative_to(repo_root.resolve())
                .as_posix()
            )
        )
    except ValueError:
        pass
    normalized_meson = normalize_repo_path(meson_build_text)
    return any(candidate in normalized_meson for candidate in candidates)


def source_occurrences(meson_build_text: str, source_path: Path, repo_root: Path) -> int:
    candidates = [normalize_repo_path(source_path.as_posix())]
    try:
        candidates.append(
            normalize_repo_path(
                source_path.resolve(strict=False)
                .relative_to(repo_root.resolve())
                .as_posix()
            )
        )
    except ValueError:
        pass
    normalized_meson = normalize_repo_path(meson_build_text)
    return max(normalized_meson.count(candidate) for candidate in candidates)


def meson_variable_mentions_source(
    meson_build_text: str,
    variable_name: str,
    source_path: Path,
    repo_root: Path,
) -> bool:
    candidates = [normalize_repo_path(source_path.as_posix())]
    try:
        candidates.append(
            normalize_repo_path(
                source_path.resolve(strict=False)
                .relative_to(repo_root.resolve())
                .as_posix()
            )
        )
    except ValueError:
        pass

    for candidate in candidates:
        source_pattern = rf"['\"]{re.escape(candidate)}['\"]"
        if re.search(
            rf"\b{re.escape(variable_name)}\s*(?:\+?=|=).*{source_pattern}",
            meson_build_text,
        ):
            return True

        list_match = re.search(
            rf"\b{re.escape(variable_name)}\s*(?:\+?=|=)\s*\[(?P<body>.*?)\]",
            meson_build_text,
            re.DOTALL,
        )
        if list_match and candidate in normalize_repo_path(list_match.group("body")):
            return True

    return False


def renderer_bridge_source_sets(
    meson_build_text: str,
    source_path: Path,
    repo_root: Path,
) -> list[str]:
    return [
        variable_name
        for variable_name in REQUIRED_RENDERER_BRIDGE_SOURCE_SETS
        if meson_variable_mentions_source(meson_build_text, variable_name, source_path, repo_root)
    ]


def guarded_include_present(source_text: str, include_token: str) -> bool:
    guard_index = source_text.find("#if UI_RML_HAS_RUNTIME")
    include_index = source_text.find(include_token)
    return guard_index >= 0 and include_index > guard_index


def extract_wrap_provide_dependencies(wrap_text: str) -> list[str]:
    in_provide = False
    dependencies: list[str] = []
    for raw_line in wrap_text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            in_provide = line.lower() == "[provide]"
            continue
        if not in_provide:
            continue
        match = re.match(r"dependency_names\s*=\s*(?P<names>.+)", line, re.IGNORECASE)
        if not match:
            continue
        dependencies.extend(
            name.strip()
            for name in match.group("names").split(",")
            if name.strip()
        )
    return dependencies


def validate_runtime_adapter(
    repo_root: Path,
    meson_build_path: Path,
    header_path: Path,
    scaffold_source_path: Path,
    adapter_source_path: Path,
    renderer_header_path: Path,
    renderer_api_source_path: Path,
    renderer_bridge_source_path: Path,
    client_renderer_source_path: Path,
    ui_bridge_source_path: Path,
    wrap_path: Path,
) -> RuntimeAdapterReport:
    repo_root = repo_root.resolve()
    meson_build_path = meson_build_path.resolve(strict=False)
    header_path = header_path.resolve(strict=False)
    scaffold_source_path = scaffold_source_path.resolve(strict=False)
    adapter_source_path = adapter_source_path.resolve(strict=False)
    renderer_header_path = renderer_header_path.resolve(strict=False)
    renderer_api_source_path = renderer_api_source_path.resolve(strict=False)
    renderer_bridge_source_path = renderer_bridge_source_path.resolve(strict=False)
    client_renderer_source_path = client_renderer_source_path.resolve(strict=False)
    ui_bridge_source_path = ui_bridge_source_path.resolve(strict=False)
    wrap_path = wrap_path.resolve(strict=False)

    meson_build_text = read_text_if_file(meson_build_path)
    header_text = read_text_if_file(header_path)
    scaffold_text = read_text_if_file(scaffold_source_path)
    adapter_text = read_text_if_file(adapter_source_path)
    renderer_header_text = read_text_if_file(renderer_header_path)
    renderer_api_text = read_text_if_file(renderer_api_source_path)
    renderer_bridge_text = read_text_if_file(renderer_bridge_source_path)
    client_renderer_text = read_text_if_file(client_renderer_source_path)
    ui_bridge_text = read_text_if_file(ui_bridge_source_path)
    wrap_text = read_text_if_file(wrap_path)

    symbols = [
        symbol
        for symbol in ("Rml::GetVersion", "Rml::Initialise", "Rml::Shutdown")
        if symbol in adapter_text
    ]
    interface_symbols = [
        symbol
        for symbol in (
            "Rml::SetSystemInterface",
            "Rml::SetFileInterface",
            "Rml::GetFileInterface",
        )
        if symbol in adapter_text
    ]
    worr_file_symbols = [
        symbol
        for symbol in (
            "FS_OpenFile",
            "FS_CloseFile",
            "FS_Read",
            "FS_Seek",
            "FS_Tell",
            "FS_Length",
        )
        if symbol in adapter_text
    ]
    worr_system_symbols = [
        symbol
        for symbol in ("Sys_Milliseconds", "Com_EPrintf", "Com_WPrintf", "Com_Printf")
        if symbol in adapter_text
    ]
    install_index = adapter_text.find("UI_Rml_InstallCoreInterfaces();")
    initialise_index = adapter_text.find("Rml::Initialise()")
    set_render_interface_index = adapter_text.find("Rml::SetRenderInterface")
    renderer_families = [
        family
        for family in (
            "UI_RML_RENDERER_FAMILY_OPENGL",
            "UI_RML_RENDERER_FAMILY_VULKAN",
            "UI_RML_RENDERER_FAMILY_RTX_VKPT",
        )
        if family in header_text
    ]
    renderer_api_families = [
        family
        for family in (
            "R_RENDERER_RMLUI_FAMILY_OPENGL",
            "R_RENDERER_RMLUI_FAMILY_VULKAN",
            "R_RENDERER_RMLUI_FAMILY_RTX_VKPT",
        )
        if family in renderer_header_text
    ]
    runtime_can_open_index = scaffold_text.find("static bool UI_Rml_RuntimeCanOpenRoutes")
    runtime_next_index = scaffold_text.find("static void UI_Rml_StopRuntime", runtime_can_open_index)
    runtime_can_open_body = (
        scaffold_text[runtime_can_open_index:runtime_next_index]
        if runtime_can_open_index >= 0 and runtime_next_index > runtime_can_open_index
        else ""
    )
    ui_draw_index = ui_bridge_text.find("void UI_Draw")
    rml_draw_index = ui_bridge_text.find("UI_Rml_Draw(realtime)", ui_draw_index)
    legacy_draw_index = ui_bridge_text.find("api->Draw(realtime)", ui_draw_index)
    vulkan_redirected_to_opengl = any(
        re.search(pattern, scaffold_text + "\n" + adapter_text, re.IGNORECASE | re.DOTALL)
        for pattern in (
            r"vulkan[-_\s]+to[-_\s]+opengl",
            r"UI_RML_RENDERER_FAMILY_VULKAN[^{};]*UI_RML_RENDERER_FAMILY_OPENGL",
            r"UI_RML_RENDERER_FAMILY_VULKAN[^{};]*opengl",
        )
    )
    bridge_meson_occurrences = source_occurrences(
        meson_build_text, renderer_bridge_source_path, repo_root
    )
    bridge_required_source_sets = renderer_bridge_source_sets(
        meson_build_text, renderer_bridge_source_path, repo_root
    )

    report = RuntimeAdapterReport(
        repo_root=repo_root,
        meson_build_path=meson_build_path,
        header_path=header_path,
        scaffold_source_path=scaffold_source_path,
        adapter_source_path=adapter_source_path,
        renderer_header_path=renderer_header_path,
        renderer_api_source_path=renderer_api_source_path,
        renderer_bridge_source_path=renderer_bridge_source_path,
        client_renderer_source_path=client_renderer_source_path,
        ui_bridge_source_path=ui_bridge_source_path,
        wrap_path=wrap_path,
        meson_build_exists=meson_build_path.is_file(),
        header_exists=header_path.is_file(),
        scaffold_source_exists=scaffold_source_path.is_file(),
        adapter_source_exists=adapter_source_path.is_file(),
        renderer_header_exists=renderer_header_path.is_file(),
        renderer_api_source_exists=renderer_api_source_path.is_file(),
        renderer_bridge_source_exists=renderer_bridge_source_path.is_file(),
        client_renderer_source_exists=client_renderer_source_path.is_file(),
        ui_bridge_source_exists=ui_bridge_source_path.is_file(),
        wrap_exists=wrap_path.is_file(),
        adapter_listed_in_meson=source_listed(meson_build_text, adapter_source_path, repo_root),
        renderer_bridge_listed_in_meson=source_listed(
            meson_build_text, renderer_bridge_source_path, repo_root
        ),
        renderer_bridge_listed_once_in_meson=(bridge_meson_occurrences == 1),
        renderer_bridge_meson_occurrences=bridge_meson_occurrences,
        renderer_bridge_required_source_sets=bridge_required_source_sets,
        renderer_bridge_required_source_sets_present=(
            tuple(bridge_required_source_sets) == REQUIRED_RENDERER_BRIDGE_SOURCE_SETS
        ),
        renderer_cpp_args_configured=(
            "renderer_cpp_args" in meson_build_text
            and "-DHAVE_CONFIG_H" in meson_build_text
            and "-DQ2PROTO_CONFIG_H=\"common/q2proto_config.h\"" in meson_build_text
            and "renderer_gl_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_GL']" in meson_build_text
            and "renderer_vk_rtx_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_VKPT'" in meson_build_text
            and "renderer_vk_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_VKPT'" in meson_build_text
        ),
        renderer_runtime_dependencies=(
            "renderer_gl_deps += rmlui_dep" in meson_build_text
            and "renderer_gl_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" in meson_build_text
            and "renderer_vk_deps += rmlui_dep" in meson_build_text
            and "renderer_vk_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" in meson_build_text
            and "renderer_vk_rtx_deps += rmlui_dep" in meson_build_text
            and "renderer_vk_rtx_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" in meson_build_text
        ),
        adapter_runtime_guard_present="#if UI_RML_HAS_RUNTIME" in adapter_text,
        rmlui_core_include_guarded=guarded_include_present(
            adapter_text, "#include <RmlUi/Core/Core.h>"
        ),
        rmlui_system_file_includes_guarded=(
            guarded_include_present(adapter_text, "#include <RmlUi/Core/SystemInterface.h>")
            and guarded_include_present(adapter_text, "#include <RmlUi/Core/FileInterface.h>")
        ),
        rmlui_render_include_guarded=guarded_include_present(
            adapter_text, "#include <RmlUi/Core/RenderInterface.h>"
        ),
        rmlui_macro_collision_guards=(
            "#undef DotProduct" in adapter_text
            and "#undef CrossProduct" in adapter_text
            and adapter_text.find("#undef DotProduct")
            < adapter_text.find("#include <RmlUi/Core/Core.h>")
            and adapter_text.find("#undef CrossProduct")
            < adapter_text.find("#include <RmlUi/Core/Core.h>")
        ),
        rmlui_core_symbols=symbols,
        rmlui_core_interface_symbols=interface_symbols,
        rmlui_render_interface_symbols=[
            symbol
            for symbol in (
                "Rml::SetRenderInterface",
                "Rml::RenderInterface",
            )
            if symbol in adapter_text
        ],
        runtime_font_engine_adapter_present=(
            guarded_include_present(adapter_text, "#include <RmlUi/Core/FontEngineInterface.h>")
            and all(
                symbol in adapter_text
                for symbol in (
                    "Rml::FontEngineInterface",
                    "Rml::SetFontEngineInterface",
                    "Rml::TexturedMesh",
                    "GetFontFaceHandle",
                    "GetFontMetrics",
                    "GetStringWidth",
                    "GenerateString",
                    "ui_rml_font_interface",
                )
            )
            and (
                (
                    "UI_Rml_TtfFontEngineInterface" in adapter_text
                    and "RmlUi TTF font engine generated text texture" in adapter_text
                    and "Rml::CallbackTexture" in adapter_text
                )
                or (
                    "UI_Rml_SmokeFontEngineInterface" in adapter_text
                    and "generated glyph geometry" in adapter_text
                )
            )
        ),
        runtime_font_engine_q2r_fonts_present=(
            "loaded from Quake II Rerelease font" in adapter_text
            and "fonts/RussoOne-Regular.ttf" in adapter_text
            and "fonts/Montserrat-Regular.ttf" in adapter_text
            and "fonts/RobotoMono-Regular.ttf" in adapter_text
            and "Quake II Rerelease:" in adapter_text
        ),
        worr_file_symbols=worr_file_symbols,
        worr_system_symbols=worr_system_symbols,
        runtime_registration_declared="UI_Rml_RegisterCompiledRuntime" in header_text,
        runtime_registration_called="UI_Rml_RegisterCompiledRuntime();" in scaffold_text,
        runtime_interface_registered="UI_Rml_SetRuntimeInterface(&runtime)" in adapter_text,
        runtime_probe_hook_present=(
            "ProbeRoute" in header_text
            and "ui_rml_runtime_probe" in scaffold_text
            and "UI_Rml_CompiledRuntimeProbeRoute" in adapter_text
            and "LoadFile(document_path" in adapter_text
        ),
        runtime_context_hooks_declared=all(
            symbol in header_text
            for symbol in (
                "CloseRoute",
                "Update)(int width, int height, unsigned realtime)",
                "Render)(void)",
                "UI_Rml_IsRouteActive",
                "UI_Rml_Draw",
                "UI_Rml_CloseActiveRoute",
            )
        ),
        runtime_context_adapter_present=(
            guarded_include_present(adapter_text, "#include <RmlUi/Core/Context.h>")
            and guarded_include_present(adapter_text, "#include <RmlUi/Core/ElementDocument.h>")
            and all(
                symbol in adapter_text
                for symbol in (
                    "Rml::Context",
                    "Rml::ElementDocument",
                    "Rml::CreateContext",
                    "Rml::RemoveContext",
                    "LoadDocument",
                    "Show()",
                    "SetDimensions",
                    "Update()",
                    "Render()",
                    "Close()",
                    "UI_Rml_CompiledRuntimeUpdate",
                    "UI_Rml_CompiledRuntimeRender",
                )
            )
        ),
        runtime_context_scaffolded=all(
            symbol in scaffold_text
            for symbol in (
                "ui_rml_route_active",
                "ui_rml_active_route",
                "UI_Rml_OpenRouteInternal",
                "UI_Rml_ClearActiveRoute(true)",
                "ui_rml_runtime.Update(width, height, realtime)",
                "ui_rml_runtime.Render()",
                "Key_SetDest",
                "KEY_MENU",
                "core.runtime_smoke",
            )
        ),
        runtime_draw_bridge_hooked=(
            ui_bridge_source_path.is_file()
            and rml_draw_index >= 0
            and legacy_draw_index >= 0
            and rml_draw_index < legacy_draw_index
            and "UI_Rml_KeyEvent(key, down)" in ui_bridge_text
            and "UI_Rml_CharEvent(key)" in ui_bridge_text
            and "UI_Rml_MouseEvent(x, y)" in ui_bridge_text
        ),
        runtime_open_commands_present=(
            "ui_rml_runtime_open" in scaffold_text
            and "ui_rml_runtime_close" in scaffold_text
            and "UI_Rml_RuntimeOpenRoute_f" in scaffold_text
            and "UI_Rml_RuntimeCloseRoute_f" in scaffold_text
        ),
        runtime_input_hooks_declared=all(
            symbol in header_text
            for symbol in (
                "KeyEvent)(int key, bool down)",
                "CharEvent)(int key)",
                "MouseEvent)(int x, int y)",
                "UI_Rml_KeyEvent",
                "UI_Rml_CharEvent",
                "UI_Rml_MouseEvent",
            )
        ),
        runtime_input_adapter_present=(
            guarded_include_present(adapter_text, "#include <RmlUi/Core/Input.h>")
            and all(
                symbol in adapter_text
                for symbol in (
                    "ProcessKeyDown",
                    "ProcessKeyUp",
                    "ProcessTextInput",
                    "ProcessMouseMove",
                    "ProcessMouseButtonDown",
                    "ProcessMouseButtonUp",
                    "ProcessMouseWheel",
                    "UI_Rml_CompiledRuntimeKeyIdentifier",
                    "UI_Rml_CompiledRuntimeModifiers",
                    "UI_Rml_CompiledRuntimeMouseButtonIndex",
                    "Rml::Input::KI_ESCAPE",
                    "Rml::Input::KM_CTRL",
                )
            )
        ),
        runtime_input_bridge_hooked=all(
            symbol in scaffold_text + "\n" + ui_bridge_text
            for symbol in (
                "ui_rml_runtime.KeyEvent",
                "ui_rml_runtime.CharEvent",
                "ui_rml_runtime.MouseEvent",
                "UI_Rml_KeyEvent(key, down)",
                "UI_Rml_CharEvent(key)",
                "UI_Rml_MouseEvent(x, y)",
                "K_MOUSE2",
                "K_MWHEELUP",
            )
        ),
        runtime_status_capture_commands_present=(
            "ui_rml_runtime_status" in scaffold_text
            and "ui_rml_runtime_capture" in scaffold_text
            and "ui_rml_runtime_capture_menu" in scaffold_text
            and "ui_rml_runtime_synthetic_input" in scaffold_text
            and "UI_Rml_RuntimeCaptureMenu_f" in scaffold_text
            and "UI_Rml_PrintRuntimeStatus" in scaffold_text
            and "UI_Rml_RuntimeSyntheticInput_f" in scaffold_text
            and "ui_rml_route_metrics" in scaffold_text
            and "updates=%u renders=%u" in scaffold_text
            and "opens=%u closes=%u close_requests=%u synthetic_inputs=%u" in scaffold_text
            and "mouse_moves=%u" in scaffold_text
        ),
        core_interfaces_installed_before_initialise=(
            install_index >= 0 and initialise_index >= 0 and install_index < initialise_index
        ),
        renderer_contract_declared=(
            "ui_rml_renderer_family_t" in header_text
            and "ui_rml_renderer_interface_t" in header_text
            and "RendererName" in header_text
            and "CanRender" in header_text
            and "NativeRenderInterface" in header_text
            and "UI_Rml_SetRendererInterface" in header_text
            and "UI_Rml_ClearRendererInterface" in header_text
            and "UI_Rml_RendererIsAvailable" in header_text
        ),
        renderer_native_families=renderer_families,
        renderer_contract_scaffolded=(
            "ui_rml_renderer" in scaffold_text
            and "ui_rml_renderer_registered" in scaffold_text
            and "UI_Rml_RendererInterface" in scaffold_text
            and "UI_Rml_RendererFamilyString" in scaffold_text
            and "UI_Rml_RendererName" in scaffold_text
            and "UI_Rml_RendererIsAvailable" in scaffold_text
        ),
        renderer_api_contract_declared=(
            "renderer_rmlui_family_t" in renderer_header_text
            and "R_RmlUiRendererFamily" in renderer_header_text
            and "R_RmlUiRendererName" in renderer_header_text
            and "R_RmlUiCanRender" in renderer_header_text
            and "R_RmlUiNativeRenderInterface" in renderer_header_text
            and "RmlUiRendererFamily" in renderer_header_text
            and "RmlUiRendererName" in renderer_header_text
            and "RmlUiCanRender" in renderer_header_text
            and "RmlUiNativeRenderInterface" in renderer_header_text
            and len(renderer_api_families) == 3
        ),
        renderer_api_exports_declared=(
            ".RmlUiRendererFamily" in renderer_api_text
            and ".RmlUiRendererName" in renderer_api_text
            and ".RmlUiCanRender" in renderer_api_text
            and ".RmlUiNativeRenderInterface" in renderer_api_text
        ),
        renderer_api_opengl_only=(
            "#if USE_REF == REF_GL" in renderer_api_text
            and ".RmlUiRendererFamily    = R_RmlUiRendererFamily" in renderer_api_text
            and ".RmlUiNativeRenderInterface = R_RmlUiNativeRenderInterface" in renderer_api_text
        ),
        renderer_api_non_gl_unavailable=(
            "#if USE_REF != REF_GL" in renderer_api_text
            and ".RmlUiRendererFamily    = R_RmlUiRendererFamily" in renderer_api_text
            and ".RmlUiRendererName      = R_RmlUiRendererName" in renderer_api_text
            and ".RmlUiCanRender         = Renderer_RmlUiCanRender" in renderer_api_text
            and ".RmlUiNativeRenderInterface = Renderer_RmlUiNativeRenderInterface" in renderer_api_text
            and "static bool Renderer_RmlUiCanRender" in renderer_api_text
            and "return false;" in renderer_api_text
            and "static void *Renderer_RmlUiNativeRenderInterface" in renderer_api_text
            and "return NULL;" in renderer_api_text
        ),
        client_renderer_bridge_registered=(
            "RmlUi_RegisterRendererBridge" in client_renderer_text
            and "UI_Rml_SetRendererInterface(&renderer)" in client_renderer_text
            and "RmlUi_RegisterRendererBridge();" in client_renderer_text
            and client_renderer_text.find("cls.ref_initialized = true;")
            < client_renderer_text.find("RmlUi_RegisterRendererBridge();")
        ),
        client_renderer_bridge_cleared=(
            "UI_Rml_ClearRendererInterface();" in client_renderer_text
            and client_renderer_text.find("UI_Shutdown();") >= 0
            and client_renderer_text.find(
                "UI_Rml_ClearRendererInterface();",
                client_renderer_text.find("UI_Shutdown();"),
            )
            > client_renderer_text.find("UI_Shutdown();")
        ),
        client_renderer_family_mapping=(
            "R_RENDERER_RMLUI_FAMILY_OPENGL" in client_renderer_text
            and "UI_RML_RENDERER_FAMILY_OPENGL" in client_renderer_text
            and "R_RENDERER_RMLUI_FAMILY_VULKAN" in client_renderer_text
            and "UI_RML_RENDERER_FAMILY_VULKAN" in client_renderer_text
            and "R_RENDERER_RMLUI_FAMILY_RTX_VKPT" in client_renderer_text
            and "UI_RML_RENDERER_FAMILY_RTX_VKPT" in client_renderer_text
        ),
        opengl_renderer_bridge_guarded=(
            "#if UI_RML_HAS_RUNTIME" in renderer_bridge_text
            and guarded_include_present(renderer_bridge_text, "#include <RmlUi/Core/RenderInterface.h>")
            and "#undef DotProduct" in renderer_bridge_text
            and "#undef CrossProduct" in renderer_bridge_text
        ),
        opengl_renderer_bridge_scaffolded=all(
            symbol in renderer_bridge_text
            for symbol in (
                "Rml::RenderInterface",
                "CompileGeometry",
                "RenderGeometry",
                "LoadTexture",
                "GenerateTexture",
                "EnableScissorRegion",
                "SetScissorRegion",
                "r_rmlui_opengl_render_interface",
            )
        ),
        opengl_renderer_bridge_family=(
            "return R_RENDERER_RMLUI_FAMILY_OPENGL;" in renderer_bridge_text
            and "OpenGL RmlUi render-interface primitives" in renderer_bridge_text
        ),
        opengl_renderer_geometry_cache=all(
            symbol in renderer_bridge_text
            for symbol in (
                "R_RmlUiCompiledGeometry",
                "std::vector<glVertexDesc2D_t>",
                "std::vector<glIndex_t>",
                "ToNonPremultiplied",
                "COLOR_U32_RGBA",
                "reinterpret_cast<Rml::CompiledGeometryHandle>",
                "delete compiled",
            )
        ),
        opengl_renderer_draw_primitives=all(
            symbol in renderer_bridge_text
            for symbol in (
                "reinterpret_cast<glVertexDesc2D_t *>(tess.vertices)",
                "tess.indices",
                "GL_Flush2D()",
                "TextureForHandle",
                "translation.x",
                "translation.y",
                "GLS_BLEND_BLEND",
                "GLS_SHADE_SMOOTH",
                "TEXNUM_WHITE",
            )
        ),
        opengl_renderer_texture_uploads=all(
            symbol in renderer_bridge_text
            for symbol in (
                "qglGenTextures",
                "GL_ForceTexture",
                "qglPixelStorei",
                "qglTexImage2D",
                "qglTexParameteri",
                "GL_TEXTURE_MIN_FILTER",
                "GL_TEXTURE_WRAP_S",
                "UnpremultiplyTexture",
                "c.texUploads",
            )
        ),
        opengl_renderer_texture_lifetime=all(
            symbol in renderer_bridge_text
            for symbol in (
                "IMG_Find",
                "RegisterTexture",
                "r_rmlui_textures",
                "qglDeleteTextures",
                "owned",
                "R_NOTEXTURE",
            )
        ),
        opengl_renderer_scissor_state=all(
            symbol in renderer_bridge_text
            for symbol in (
                "qglEnable(GL_SCISSOR_TEST)",
                "qglDisable(GL_SCISSOR_TEST)",
                "qglScissor",
                "r_config.height",
                "draw.scissor",
            )
        ),
        opengl_renderer_can_render_true=(
            re.search(
                r"R_RmlUiCanRender\s*\([^)]*\)\s*\{[^{}]*return\s+true\s*;",
                renderer_bridge_text,
                re.DOTALL,
            )
            is not None
        ),
        renderer_route_gate_present=(
            "UI_Rml_RendererIsAvailable()" in runtime_can_open_body
            and "ui_rml_runtime.CanOpenRoutes" in runtime_can_open_body
            and runtime_can_open_body.find("UI_Rml_RendererIsAvailable()")
            < runtime_can_open_body.find("ui_rml_runtime.CanOpenRoutes")
        ),
        renderer_native_interface_required=(
            "NativeRenderInterface" in scaffold_text
            and "NativeRenderInterface() != NULL" in scaffold_text
        ),
        vulkan_renderer_not_redirected=not vulkan_redirected_to_opengl,
        renderer_unavailable_state=(
            "UI_RML_AVAILABILITY_RENDERER_UNAVAILABLE" in header_text
            and "renderer_unavailable" in scaffold_text
        ),
        route_open_guard_present=(
            "CanOpenRoutes" in header_text
            and "UI_Rml_CompiledRuntimeCanOpenRoutes" in adapter_text
            and "UI_Rml_CompiledRuntimeRouteIsAllowed" in adapter_text
            and "UI_Rml_RuntimeRouteIsAllowed" in scaffold_text
            and "UI_Rml_FindRuntimeMenuRoute" in scaffold_text
            and "ui_rml_runtime_menu_routes" in scaffold_text
            and (
                all(route in adapter_text for route in GUARDED_RUNTIME_ROUTES)
                or "return route_id && route_id[0];" in adapter_text
            )
            and all(route in scaffold_text for route in GUARDED_RUNTIME_ROUTES)
            and "UI_RML_RENDERER_FAMILY_OPENGL" in adapter_text
        ),
        cmake_font_engine_none=(
            "RMLUI_FONT_ENGINE" in meson_build_text and "'none'" in meson_build_text
        ),
        cmake_samples_disabled=(
            "RMLUI_SAMPLES" in meson_build_text and "false" in meson_build_text
        ),
        cmake_tests_disabled=(
            "RMLUI_TESTS" in meson_build_text and "false" in meson_build_text
        ),
        wrap_provide_dependencies=extract_wrap_provide_dependencies(wrap_text),
    )

    add_consistency_findings(report)
    return report


def add_consistency_findings(report: RuntimeAdapterReport) -> None:
    if not report.meson_build_exists:
        report.errors.append(
            f"meson: missing {display_path(report.meson_build_path, report.repo_root)}"
        )
    if not report.header_exists:
        report.errors.append(
            f"header: missing {display_path(report.header_path, report.repo_root)}"
        )
    if not report.scaffold_source_exists:
        report.errors.append(
            f"scaffold: missing {display_path(report.scaffold_source_path, report.repo_root)}"
        )
    if not report.adapter_source_exists:
        report.errors.append(
            f"adapter: missing {display_path(report.adapter_source_path, report.repo_root)}"
        )
    if not report.renderer_header_exists:
        report.errors.append(
            f"renderer header: missing {display_path(report.renderer_header_path, report.repo_root)}"
        )
    if not report.renderer_api_source_exists:
        report.errors.append(
            f"renderer api: missing {display_path(report.renderer_api_source_path, report.repo_root)}"
        )
    if not report.renderer_bridge_source_exists:
        report.errors.append(
            f"renderer bridge: missing {display_path(report.renderer_bridge_source_path, report.repo_root)}"
        )
    if not report.client_renderer_source_exists:
        report.errors.append(
            f"client renderer: missing {display_path(report.client_renderer_source_path, report.repo_root)}"
        )
    if not report.ui_bridge_source_exists:
        report.errors.append(
            f"ui bridge: missing {display_path(report.ui_bridge_source_path, report.repo_root)}"
        )
    if report.adapter_source_exists and not report.adapter_listed_in_meson:
        report.errors.append("adapter: source exists but is not listed in meson.build")
    if report.adapter_listed_in_meson and not report.adapter_source_exists:
        report.errors.append("adapter: source is listed in meson.build but missing")
    if report.renderer_bridge_source_exists and not report.renderer_bridge_listed_in_meson:
        report.errors.append("renderer bridge: source exists but is not listed in meson.build")
    if report.renderer_bridge_listed_in_meson and not report.renderer_bridge_source_exists:
        report.errors.append("renderer bridge: source is listed in meson.build but missing")
    if (
        report.renderer_bridge_source_exists
        and not report.renderer_bridge_required_source_sets_present
    ):
        missing_source_sets = [
            source_set
            for source_set in REQUIRED_RENDERER_BRIDGE_SOURCE_SETS
            if source_set not in report.renderer_bridge_required_source_sets
        ]
        report.errors.append(
            "renderer bridge: source must be listed in renderer source sets: "
            + ", ".join(missing_source_sets)
        )
    if not report.renderer_cpp_args_configured:
        report.errors.append("meson: renderer C++ args must include config defines and per-family USE_REF flags")
    if not report.renderer_runtime_dependencies:
        report.errors.append("meson: renderer DLLs must link the RmlUi runtime dependency when enabled")
    if report.adapter_source_exists and not report.adapter_runtime_guard_present:
        report.errors.append("adapter: missing UI_RML_HAS_RUNTIME compile guard")
    if report.adapter_source_exists and not report.rmlui_core_include_guarded:
        report.errors.append("adapter: RmlUi Core include is not behind UI_RML_HAS_RUNTIME")
    if report.adapter_source_exists and not report.rmlui_system_file_includes_guarded:
        report.errors.append("adapter: RmlUi system/file includes are not behind UI_RML_HAS_RUNTIME")
    if report.adapter_source_exists and not report.rmlui_render_include_guarded:
        report.errors.append("adapter: RmlUi render include is not behind UI_RML_HAS_RUNTIME")
    if report.adapter_source_exists and not report.rmlui_macro_collision_guards:
        report.errors.append("adapter: missing RmlUi macro collision guards")
    missing_symbols = {
        "Rml::GetVersion",
        "Rml::Initialise",
        "Rml::Shutdown",
    }.difference(report.rmlui_core_symbols)
    if missing_symbols:
        report.errors.append(
            "adapter: missing RmlUi Core symbol references: "
            + ", ".join(sorted(missing_symbols))
        )
    missing_interface_symbols = {
        "Rml::SetSystemInterface",
        "Rml::SetFileInterface",
        "Rml::GetFileInterface",
    }.difference(report.rmlui_core_interface_symbols)
    if missing_interface_symbols:
        report.errors.append(
            "adapter: missing RmlUi system/file interface symbols: "
            + ", ".join(sorted(missing_interface_symbols))
        )
    missing_render_symbols = {
        "Rml::SetRenderInterface",
        "Rml::RenderInterface",
    }.difference(report.rmlui_render_interface_symbols)
    if missing_render_symbols:
        report.errors.append(
            "adapter: missing RmlUi render interface symbols: "
            + ", ".join(sorted(missing_render_symbols))
        )
    if not report.runtime_font_engine_adapter_present:
        report.errors.append("adapter: missing guarded RmlUi glyph-generating font engine install")
    if not report.runtime_font_engine_q2r_fonts_present:
        report.errors.append("adapter: missing Quake II Rerelease TTF font candidates/source marker")
    missing_file_symbols = {
        "FS_OpenFile",
        "FS_CloseFile",
        "FS_Read",
        "FS_Seek",
        "FS_Tell",
        "FS_Length",
    }.difference(report.worr_file_symbols)
    if missing_file_symbols:
        report.errors.append(
            "adapter: missing WORR filesystem symbols: "
            + ", ".join(sorted(missing_file_symbols))
        )
    missing_system_symbols = {
        "Sys_Milliseconds",
        "Com_WPrintf",
        "Com_Printf",
    }.difference(report.worr_system_symbols)
    if missing_system_symbols:
        report.errors.append(
            "adapter: missing WORR system/log symbols: "
            + ", ".join(sorted(missing_system_symbols))
        )
    if not report.runtime_registration_declared:
        report.errors.append("header: missing compiled runtime registration declaration")
    if not report.runtime_registration_called:
        report.errors.append("scaffold: compiled runtime registration is not called")
    if not report.runtime_interface_registered:
        report.errors.append("adapter: runtime interface is not registered")
    if not report.runtime_probe_hook_present:
        report.errors.append("adapter: missing runtime file-probe hook and command")
    if not report.runtime_context_hooks_declared:
        report.errors.append("header: missing RmlUi runtime context lifecycle hooks")
    if not report.runtime_context_adapter_present:
        report.errors.append("adapter: missing guarded RmlUi context load/update/render lifecycle")
    if not report.runtime_context_scaffolded:
        report.errors.append("scaffold: missing guarded active-route context state")
    if not report.runtime_draw_bridge_hooked:
        report.errors.append("ui bridge: RmlUi draw path is not hooked before legacy UI draw")
    if not report.runtime_open_commands_present:
        report.errors.append("scaffold: missing guarded RmlUi runtime open/close commands")
    if not report.runtime_input_hooks_declared:
        report.errors.append("header: missing RmlUi runtime input lifecycle hooks")
    if not report.runtime_input_adapter_present:
        report.errors.append("adapter: missing guarded RmlUi key/text/pointer event delivery")
    if not report.runtime_input_bridge_hooked:
        report.errors.append("ui bridge: RmlUi input path is not hooked before legacy UI input")
    if not report.runtime_status_capture_commands_present:
        report.errors.append("scaffold: missing guarded RmlUi status/capture/synthetic-input commands")
    if not report.core_interfaces_installed_before_initialise:
        report.errors.append("adapter: RmlUi system/file interfaces must be installed before Initialise")
    if not report.renderer_contract_declared:
        report.errors.append("header: missing native renderer bridge contract declarations")
    missing_renderer_families = {
        "UI_RML_RENDERER_FAMILY_OPENGL",
        "UI_RML_RENDERER_FAMILY_VULKAN",
        "UI_RML_RENDERER_FAMILY_RTX_VKPT",
    }.difference(report.renderer_native_families)
    if missing_renderer_families:
        report.errors.append(
            "header: missing native renderer families: "
            + ", ".join(sorted(missing_renderer_families))
        )
    if not report.renderer_contract_scaffolded:
        report.errors.append("scaffold: missing native renderer bridge scaffold")
    if not report.renderer_api_contract_declared:
        report.errors.append("renderer header: missing renderer-side RmlUi bridge API contract")
    if not report.renderer_api_exports_declared:
        report.errors.append("renderer api: missing RmlUi bridge export slots")
    if not report.renderer_api_opengl_only:
        report.errors.append("renderer api: OpenGL renderer must be the only concrete RmlUi bridge export")
    if not report.renderer_api_non_gl_unavailable:
        report.errors.append("renderer api: non-OpenGL renderers must keep RmlUi rendering unavailable")
    if not report.client_renderer_bridge_registered:
        report.errors.append("client renderer: native RmlUi bridge is not registered after renderer init")
    if not report.client_renderer_bridge_cleared:
        report.errors.append("client renderer: native RmlUi bridge is not cleared during renderer shutdown")
    if not report.client_renderer_family_mapping:
        report.errors.append("client renderer: missing renderer family mapping for OpenGL, Vulkan, and RTX/vkpt")
    if not report.opengl_renderer_bridge_guarded:
        report.errors.append("renderer bridge: OpenGL RmlUi bridge must be guarded by UI_RML_HAS_RUNTIME")
    if not report.opengl_renderer_bridge_scaffolded:
        report.errors.append("renderer bridge: missing OpenGL RmlUi RenderInterface scaffold methods")
    if not report.opengl_renderer_geometry_cache:
        report.errors.append("renderer bridge: missing OpenGL compiled geometry cache and color conversion")
    if not report.opengl_renderer_draw_primitives:
        report.errors.append("renderer bridge: missing OpenGL tessellator draw primitive path")
    if not report.opengl_renderer_texture_uploads:
        report.errors.append("renderer bridge: missing OpenGL generated texture upload path")
    if not report.opengl_renderer_texture_lifetime:
        report.errors.append("renderer bridge: missing OpenGL loaded/generated texture lifetime tracking")
    if not report.opengl_renderer_scissor_state:
        report.errors.append("renderer bridge: missing OpenGL scissor state handling")
    if not report.opengl_renderer_bridge_family:
        report.errors.append("renderer bridge: OpenGL primitive bridge must report the OpenGL renderer family")
    if not report.opengl_renderer_can_render_true:
        report.errors.append("renderer bridge: OpenGL primitive bridge must report CanRender true")
    if not report.renderer_route_gate_present:
        report.errors.append("scaffold: route availability is not gated by native renderer availability")
    if not report.renderer_native_interface_required:
        report.errors.append("scaffold: native renderer availability must require an opaque render interface")
    if not report.vulkan_renderer_not_redirected:
        report.errors.append("scaffold: Vulkan renderer path must not redirect to OpenGL")
    if not report.renderer_unavailable_state:
        report.errors.append("scaffold: missing renderer_unavailable availability state")
    if not report.route_open_guard_present:
        report.errors.append("adapter: missing guarded runtime-route open guard")
    if not report.cmake_font_engine_none:
        report.errors.append("meson: RmlUi CMake fallback must set RMLUI_FONT_ENGINE to none")
    if not report.cmake_samples_disabled:
        report.errors.append("meson: RmlUi CMake fallback must disable RMLUI_SAMPLES")
    if not report.cmake_tests_disabled:
        report.errors.append("meson: RmlUi CMake fallback must disable RMLUI_TESTS")
    if not report.wrap_exists:
        report.errors.append(f"wrap: missing {display_path(report.wrap_path, report.repo_root)}")
    provide_names = {name.lower() for name in report.wrap_provide_dependencies}
    if not {"rmlui"}.issubset(provide_names):
        report.errors.append("wrap: [provide] dependency_names must include rmlui")


def json_report_payload(report: RuntimeAdapterReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "paths": {
            "repo_root": str(report.repo_root),
            "meson_build": display_path(report.meson_build_path, report.repo_root),
            "header": display_path(report.header_path, report.repo_root),
            "scaffold_source": display_path(report.scaffold_source_path, report.repo_root),
            "adapter_source": display_path(report.adapter_source_path, report.repo_root),
            "renderer_header": display_path(report.renderer_header_path, report.repo_root),
            "renderer_api_source": display_path(report.renderer_api_source_path, report.repo_root),
            "renderer_bridge_source": display_path(report.renderer_bridge_source_path, report.repo_root),
            "client_renderer_source": display_path(report.client_renderer_source_path, report.repo_root),
            "ui_bridge_source": display_path(report.ui_bridge_source_path, report.repo_root),
            "wrap": display_path(report.wrap_path, report.repo_root),
        },
        "files": {
            "meson_build_exists": report.meson_build_exists,
            "header_exists": report.header_exists,
            "scaffold_source_exists": report.scaffold_source_exists,
            "adapter_source_exists": report.adapter_source_exists,
            "renderer_header_exists": report.renderer_header_exists,
            "renderer_api_source_exists": report.renderer_api_source_exists,
            "renderer_bridge_source_exists": report.renderer_bridge_source_exists,
            "client_renderer_source_exists": report.client_renderer_source_exists,
            "ui_bridge_source_exists": report.ui_bridge_source_exists,
            "wrap_exists": report.wrap_exists,
        },
        "adapter": {
            "listed_in_meson": report.adapter_listed_in_meson,
            "renderer_bridge_listed_in_meson": report.renderer_bridge_listed_in_meson,
            "renderer_bridge_listed_once_in_meson": report.renderer_bridge_listed_once_in_meson,
            "renderer_bridge_meson_occurrences": report.renderer_bridge_meson_occurrences,
            "renderer_bridge_required_source_sets": report.renderer_bridge_required_source_sets,
            "renderer_bridge_required_source_sets_present": (
                report.renderer_bridge_required_source_sets_present
            ),
            "renderer_cpp_args_configured": report.renderer_cpp_args_configured,
            "renderer_runtime_dependencies": report.renderer_runtime_dependencies,
            "runtime_guard_present": report.adapter_runtime_guard_present,
            "rmlui_core_include_guarded": report.rmlui_core_include_guarded,
            "rmlui_system_file_includes_guarded": report.rmlui_system_file_includes_guarded,
            "rmlui_render_include_guarded": report.rmlui_render_include_guarded,
            "rmlui_macro_collision_guards": report.rmlui_macro_collision_guards,
            "rmlui_core_symbols": report.rmlui_core_symbols,
            "rmlui_core_interface_symbols": report.rmlui_core_interface_symbols,
            "rmlui_render_interface_symbols": report.rmlui_render_interface_symbols,
            "runtime_font_engine_adapter_present": report.runtime_font_engine_adapter_present,
            "runtime_font_engine_q2r_fonts_present": report.runtime_font_engine_q2r_fonts_present,
            "worr_file_symbols": report.worr_file_symbols,
            "worr_system_symbols": report.worr_system_symbols,
            "runtime_registration_declared": report.runtime_registration_declared,
            "runtime_registration_called": report.runtime_registration_called,
            "runtime_interface_registered": report.runtime_interface_registered,
            "runtime_probe_hook_present": report.runtime_probe_hook_present,
            "runtime_context_hooks_declared": report.runtime_context_hooks_declared,
            "runtime_context_adapter_present": report.runtime_context_adapter_present,
            "runtime_context_scaffolded": report.runtime_context_scaffolded,
            "runtime_draw_bridge_hooked": report.runtime_draw_bridge_hooked,
            "runtime_open_commands_present": report.runtime_open_commands_present,
            "runtime_input_hooks_declared": report.runtime_input_hooks_declared,
            "runtime_input_adapter_present": report.runtime_input_adapter_present,
            "runtime_input_bridge_hooked": report.runtime_input_bridge_hooked,
            "runtime_status_capture_commands_present": report.runtime_status_capture_commands_present,
            "core_interfaces_installed_before_initialise": report.core_interfaces_installed_before_initialise,
            "renderer_contract_declared": report.renderer_contract_declared,
            "renderer_native_families": report.renderer_native_families,
            "renderer_contract_scaffolded": report.renderer_contract_scaffolded,
            "renderer_api_contract_declared": report.renderer_api_contract_declared,
            "renderer_api_exports_declared": report.renderer_api_exports_declared,
            "renderer_api_opengl_only": report.renderer_api_opengl_only,
            "renderer_api_non_gl_unavailable": report.renderer_api_non_gl_unavailable,
            "client_renderer_bridge_registered": report.client_renderer_bridge_registered,
            "client_renderer_bridge_cleared": report.client_renderer_bridge_cleared,
            "client_renderer_family_mapping": report.client_renderer_family_mapping,
            "opengl_renderer_bridge_guarded": report.opengl_renderer_bridge_guarded,
            "opengl_renderer_bridge_scaffolded": report.opengl_renderer_bridge_scaffolded,
            "opengl_renderer_geometry_cache": report.opengl_renderer_geometry_cache,
            "opengl_renderer_draw_primitives": report.opengl_renderer_draw_primitives,
            "opengl_renderer_texture_uploads": report.opengl_renderer_texture_uploads,
            "opengl_renderer_texture_lifetime": report.opengl_renderer_texture_lifetime,
            "opengl_renderer_scissor_state": report.opengl_renderer_scissor_state,
            "opengl_renderer_bridge_family": report.opengl_renderer_bridge_family,
            "opengl_renderer_can_render_true": report.opengl_renderer_can_render_true,
            "renderer_route_gate_present": report.renderer_route_gate_present,
            "renderer_native_interface_required": report.renderer_native_interface_required,
            "vulkan_renderer_not_redirected": report.vulkan_renderer_not_redirected,
            "renderer_unavailable_state": report.renderer_unavailable_state,
            "route_open_guard_present": report.route_open_guard_present,
            "cmake_font_engine_none": report.cmake_font_engine_none,
            "cmake_samples_disabled": report.cmake_samples_disabled,
            "cmake_tests_disabled": report.cmake_tests_disabled,
        },
        "wrap": {
            "provide_dependencies": report.wrap_provide_dependencies,
        },
        "counts": {
            "rmlui_core_symbols": len(report.rmlui_core_symbols),
            "rmlui_core_interface_symbols": len(report.rmlui_core_interface_symbols),
            "rmlui_render_interface_symbols": len(report.rmlui_render_interface_symbols),
            "worr_file_symbols": len(report.worr_file_symbols),
            "worr_system_symbols": len(report.worr_system_symbols),
            "renderer_native_families": len(report.renderer_native_families),
            "wrap_provide_dependencies": len(report.wrap_provide_dependencies),
            "errors": len(report.errors),
        },
        "errors": report.errors,
    }


def print_json_report(report: RuntimeAdapterReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def yes_no(value: bool) -> str:
    return "yes" if value else "no"


def print_report(report: RuntimeAdapterReport) -> None:
    print("RmlUi runtime adapter boundary:")
    print(f"  Malformed findings: {len(report.errors)}")
    print(f"  Meson build: {display_path(report.meson_build_path, report.repo_root)}")
    print(f"  Header: {display_path(report.header_path, report.repo_root)}")
    print(f"  Scaffold: {display_path(report.scaffold_source_path, report.repo_root)}")
    print(f"  Adapter: {display_path(report.adapter_source_path, report.repo_root)}")
    print(f"  Renderer header: {display_path(report.renderer_header_path, report.repo_root)}")
    print(f"  Renderer API: {display_path(report.renderer_api_source_path, report.repo_root)}")
    print(f"  Renderer bridge: {display_path(report.renderer_bridge_source_path, report.repo_root)}")
    print(f"  Client renderer: {display_path(report.client_renderer_source_path, report.repo_root)}")
    print(f"  UI bridge: {display_path(report.ui_bridge_source_path, report.repo_root)}")
    print(f"  Wrap: {display_path(report.wrap_path, report.repo_root)}")

    print("\nAdapter facts:")
    print(f"  Listed in meson.build: {yes_no(report.adapter_listed_in_meson)}")
    print(f"  Renderer bridge listed in meson.build: {yes_no(report.renderer_bridge_listed_in_meson)}")
    print(f"  Renderer bridge Meson occurrences: {report.renderer_bridge_meson_occurrences}")
    print(
        "  Renderer bridge required source sets: "
        + (
            ", ".join(report.renderer_bridge_required_source_sets)
            if report.renderer_bridge_required_source_sets
            else "-"
        )
    )
    print(
        "  Renderer bridge required source-set coverage: "
        + yes_no(report.renderer_bridge_required_source_sets_present)
    )
    print(f"  Renderer C++ args configured: {yes_no(report.renderer_cpp_args_configured)}")
    print(f"  Renderer RmlUi runtime dependencies: {yes_no(report.renderer_runtime_dependencies)}")
    print(f"  Runtime guard present: {yes_no(report.adapter_runtime_guard_present)}")
    print(f"  RmlUi Core include guarded: {yes_no(report.rmlui_core_include_guarded)}")
    print(f"  RmlUi system/file includes guarded: {yes_no(report.rmlui_system_file_includes_guarded)}")
    print(f"  RmlUi render include guarded: {yes_no(report.rmlui_render_include_guarded)}")
    print(f"  RmlUi macro collision guards: {yes_no(report.rmlui_macro_collision_guards)}")
    print(
        "  RmlUi Core symbols: "
        + (", ".join(report.rmlui_core_symbols) if report.rmlui_core_symbols else "-")
    )
    print(
        "  RmlUi interface symbols: "
        + (
            ", ".join(report.rmlui_core_interface_symbols)
            if report.rmlui_core_interface_symbols
            else "-"
        )
    )
    print(
        "  RmlUi render interface symbols: "
        + (
            ", ".join(report.rmlui_render_interface_symbols)
            if report.rmlui_render_interface_symbols
            else "-"
        )
    )
    print(f"  Runtime font engine adapter: {yes_no(report.runtime_font_engine_adapter_present)}")
    print(f"  Runtime Q2R font candidates: {yes_no(report.runtime_font_engine_q2r_fonts_present)}")
    print(
        "  WORR filesystem symbols: "
        + (", ".join(report.worr_file_symbols) if report.worr_file_symbols else "-")
    )
    print(
        "  WORR system/log symbols: "
        + (", ".join(report.worr_system_symbols) if report.worr_system_symbols else "-")
    )
    print(f"  Registration declared: {yes_no(report.runtime_registration_declared)}")
    print(f"  Registration called: {yes_no(report.runtime_registration_called)}")
    print(f"  Runtime interface registered: {yes_no(report.runtime_interface_registered)}")
    print(f"  Runtime file-probe hook: {yes_no(report.runtime_probe_hook_present)}")
    print(f"  Runtime context hooks declared: {yes_no(report.runtime_context_hooks_declared)}")
    print(f"  Runtime context adapter: {yes_no(report.runtime_context_adapter_present)}")
    print(f"  Runtime active-route scaffold: {yes_no(report.runtime_context_scaffolded)}")
    print(f"  UI bridge draw hook: {yes_no(report.runtime_draw_bridge_hooked)}")
    print(f"  Runtime open/close commands: {yes_no(report.runtime_open_commands_present)}")
    print(f"  Runtime input hooks declared: {yes_no(report.runtime_input_hooks_declared)}")
    print(f"  Runtime input adapter: {yes_no(report.runtime_input_adapter_present)}")
    print(f"  UI bridge input hook: {yes_no(report.runtime_input_bridge_hooked)}")
    print(f"  Runtime status/capture commands: {yes_no(report.runtime_status_capture_commands_present)}")
    print(
        "  Core interfaces installed before Initialise: "
        + yes_no(report.core_interfaces_installed_before_initialise)
    )
    print(f"  Renderer contract declared: {yes_no(report.renderer_contract_declared)}")
    print(
        "  Renderer native families: "
        + (", ".join(report.renderer_native_families) if report.renderer_native_families else "-")
    )
    print(f"  Renderer scaffolded: {yes_no(report.renderer_contract_scaffolded)}")
    print(f"  Renderer API contract declared: {yes_no(report.renderer_api_contract_declared)}")
    print(f"  Renderer API exports declared: {yes_no(report.renderer_api_exports_declared)}")
    print(f"  Renderer API OpenGL only: {yes_no(report.renderer_api_opengl_only)}")
    print(f"  Renderer API non-OpenGL unavailable: {yes_no(report.renderer_api_non_gl_unavailable)}")
    print(f"  Client renderer bridge registered: {yes_no(report.client_renderer_bridge_registered)}")
    print(f"  Client renderer bridge cleared: {yes_no(report.client_renderer_bridge_cleared)}")
    print(f"  Client renderer family mapping: {yes_no(report.client_renderer_family_mapping)}")
    print(f"  OpenGL renderer bridge guarded: {yes_no(report.opengl_renderer_bridge_guarded)}")
    print(f"  OpenGL renderer bridge scaffolded: {yes_no(report.opengl_renderer_bridge_scaffolded)}")
    print(f"  OpenGL geometry cache: {yes_no(report.opengl_renderer_geometry_cache)}")
    print(f"  OpenGL draw primitives: {yes_no(report.opengl_renderer_draw_primitives)}")
    print(f"  OpenGL texture uploads: {yes_no(report.opengl_renderer_texture_uploads)}")
    print(f"  OpenGL texture lifetime: {yes_no(report.opengl_renderer_texture_lifetime)}")
    print(f"  OpenGL scissor state: {yes_no(report.opengl_renderer_scissor_state)}")
    print(f"  OpenGL renderer bridge family: {yes_no(report.opengl_renderer_bridge_family)}")
    print(f"  OpenGL renderer CanRender true: {yes_no(report.opengl_renderer_can_render_true)}")
    print(f"  Renderer route gate: {yes_no(report.renderer_route_gate_present)}")
    print(f"  Native render interface required: {yes_no(report.renderer_native_interface_required)}")
    print(f"  Vulkan not redirected to OpenGL: {yes_no(report.vulkan_renderer_not_redirected)}")
    print(f"  Renderer-unavailable state: {yes_no(report.renderer_unavailable_state)}")
    print(f"  Route-open guard: {yes_no(report.route_open_guard_present)}")
    print(f"  CMake font engine none: {yes_no(report.cmake_font_engine_none)}")
    print(f"  CMake samples disabled: {yes_no(report.cmake_samples_disabled)}")
    print(f"  CMake tests disabled: {yes_no(report.cmake_tests_disabled)}")

    print("\nWrap facts:")
    print(
        "  Provide dependencies: "
        + (
            ", ".join(report.wrap_provide_dependencies)
            if report.wrap_provide_dependencies
            else "-"
        )
    )

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi runtime adapter boundary check failed.")
    else:
        print("\nResult: RmlUi runtime adapter boundary check passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve default scan paths.",
    )
    parser.add_argument(
        "--meson-build",
        type=Path,
        default=DEFAULT_MESON_BUILD,
        help="Path to the root meson.build file.",
    )
    parser.add_argument(
        "--header",
        type=Path,
        default=DEFAULT_HEADER,
        help="Path to the RmlUi scaffold header.",
    )
    parser.add_argument(
        "--scaffold-source",
        type=Path,
        default=DEFAULT_SCAFFOLD_SOURCE,
        help="Path to the dependency-free RmlUi scaffold source.",
    )
    parser.add_argument(
        "--adapter-source",
        type=Path,
        default=DEFAULT_ADAPTER_SOURCE,
        help="Path to the compiled RmlUi runtime adapter source.",
    )
    parser.add_argument(
        "--renderer-header",
        type=Path,
        default=DEFAULT_RENDERER_HEADER,
        help="Path to the renderer API header.",
    )
    parser.add_argument(
        "--renderer-api-source",
        type=Path,
        default=DEFAULT_RENDERER_API_SOURCE,
        help="Path to the renderer export API source.",
    )
    parser.add_argument(
        "--renderer-bridge-source",
        type=Path,
        default=DEFAULT_RENDERER_BRIDGE_SOURCE,
        help="Path to the native RmlUi renderer bridge source.",
    )
    parser.add_argument(
        "--client-renderer-source",
        type=Path,
        default=DEFAULT_CLIENT_RENDERER_SOURCE,
        help="Path to the client renderer lifecycle source.",
    )
    parser.add_argument(
        "--ui-bridge-source",
        type=Path,
        default=DEFAULT_UI_BRIDGE_SOURCE,
        help="Path to the client UI bridge source.",
    )
    parser.add_argument(
        "--wrap",
        type=Path,
        default=DEFAULT_WRAP,
        help="Path to the RmlUi wrap file.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format. Defaults to the text report.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    report = validate_runtime_adapter(
        repo_root=repo_root,
        meson_build_path=resolve_path(args.meson_build, repo_root),
        header_path=resolve_path(args.header, repo_root),
        scaffold_source_path=resolve_path(args.scaffold_source, repo_root),
        adapter_source_path=resolve_path(args.adapter_source, repo_root),
        renderer_header_path=resolve_path(args.renderer_header, repo_root),
        renderer_api_source_path=resolve_path(args.renderer_api_source, repo_root),
        renderer_bridge_source_path=resolve_path(args.renderer_bridge_source, repo_root),
        client_renderer_source_path=resolve_path(args.client_renderer_source, repo_root),
        ui_bridge_source_path=resolve_path(args.ui_bridge_source, repo_root),
        wrap_path=resolve_path(args.wrap, repo_root),
    )

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
