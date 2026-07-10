#!/usr/bin/env python3
"""Audit Vulkan/RTX readiness for future native RmlUi renderer bridges."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_INPUTS = {
    "meson_build": Path("meson.build"),
    "renderer_header": Path("inc/renderer/renderer.h"),
    "renderer_api_source": Path("src/renderer/renderer_api.c"),
    "renderer_bridge_source": Path("src/renderer/rmlui_bridge.cpp"),
    "client_renderer_source": Path("src/client/renderer.cpp"),
    "vulkan_ui_header": Path("src/rend_vk/vk_ui.h"),
    "vulkan_ui_source": Path("src/rend_vk/vk_ui.c"),
    "vulkan_main_source": Path("src/rend_vk/vk_main.c"),
    "rtx_header": Path("src/rend_rtx/vkpt/vkpt.h"),
    "rtx_draw_source": Path("src/rend_rtx/vkpt/draw.c"),
    "rtx_main_source": Path("src/rend_rtx/vkpt/main.c"),
    "rtx_images_source": Path("src/rend_rtx/refresh/images.c"),
    "rtx_textures_source": Path("src/rend_rtx/vkpt/textures.c"),
    "rtx_stretch_pic_vertex_shader": Path("src/rend_rtx/vkpt/shader/stretch_pic.vert"),
    "rtx_stretch_pic_fragment_shader": Path("src/rend_rtx/vkpt/shader/stretch_pic.frag"),
}

LANE_LABELS = {
    "vulkan": "Vulkan",
    "rtx_vkpt": "RTX/vkpt",
}

ACTIVATION_REQUIREMENT_ORDER = (
    "native_bridge_class_present",
    "native_bridge_source_compiled",
    "native_family_export_present",
    "runtime_dependency_enabled",
    "native_interface_export_present",
)


@dataclass
class BridgeReadinessLane:
    lane_id: str
    label: str
    expected_status: str = "blocked_until_native"
    foundations: dict[str, bool] = field(default_factory=dict)
    guardrails: dict[str, bool] = field(default_factory=dict)
    activation_requirements: dict[str, bool] = field(default_factory=dict)
    missing_bridge_requirements: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def foundation_ok(self) -> bool:
        return bool(self.foundations) and all(self.foundations.values())

    def guardrails_ok(self) -> bool:
        return bool(self.guardrails) and all(self.guardrails.values())

    def native_bridge_claimed(self) -> bool:
        return not self.missing_bridge_requirements

    def activation_requirement_count(self) -> int:
        return len(self.activation_requirements)

    def satisfied_activation_requirement_count(self) -> int:
        return sum(1 for value in self.activation_requirements.values() if value)

    def pending_activation_requirement_count(self) -> int:
        return sum(1 for value in self.activation_requirements.values() if not value)

    def satisfied_activation_requirement_ids(self) -> list[str]:
        return [
            requirement_id
            for requirement_id in ACTIVATION_REQUIREMENT_ORDER
            if self.activation_requirements.get(requirement_id) is True
        ]

    def pending_activation_requirement_ids(self) -> list[str]:
        return [
            requirement_id
            for requirement_id in ACTIVATION_REQUIREMENT_ORDER
            if self.activation_requirements.get(requirement_id) is False
        ]

    def next_activation_requirement(self) -> str | None:
        pending = self.pending_activation_requirement_ids()
        return pending[0] if pending else None

    def activation_complete(self) -> bool:
        return (
            self.activation_requirement_count() > 0
            and self.pending_activation_requirement_count() == 0
        )

    def partial_activation_claimed(self) -> bool:
        return (
            self.satisfied_activation_requirement_count() > 0
            and not self.activation_complete()
        )

    def activation_status(self) -> str:
        if self.activation_complete():
            return "activation_complete"
        if self.partial_activation_claimed():
            return "partial_activation_blocked"
        return "blocked_no_activation"

    def ok(self) -> bool:
        return self.foundation_ok() and self.guardrails_ok() and not self.errors


@dataclass
class BridgeReadinessReport:
    repo_root: Path
    paths: dict[str, Path]
    files: dict[str, bool] = field(default_factory=dict)
    contract_facts: dict[str, bool] = field(default_factory=dict)
    lanes: dict[str, BridgeReadinessLane] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    def all_errors(self) -> list[str]:
        errors = list(self.errors)
        for lane in self.lanes.values():
            errors.extend(lane.errors)
        return errors

    def ok(self) -> bool:
        return not self.all_errors()


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_path(path: Path, repo_root: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve(strict=False).relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def read_text_if_file(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def yes_no(value: bool) -> str:
    return "yes" if value else "no"


def has_token(source_text: str, token: str) -> bool:
    return re.search(rf"\b{re.escape(token)}\b", source_text) is not None


def has_all_tokens(source_text: str, tokens: tuple[str, ...]) -> bool:
    return all(has_token(source_text, token) for token in tokens)


def has_return(source_text: str, value: str) -> bool:
    return re.search(rf"\breturn\s+{re.escape(value)}\s*;", source_text) is not None


def find_if_block(source_text: str, directive: str) -> str:
    pattern = re.compile(
        rf"#if\s+{re.escape(directive)}(?P<body>.*?)(?:#else.*?)*#endif",
        re.DOTALL,
    )
    match = pattern.search(source_text)
    if not match:
        return ""
    return match.group("body")


def has_export_assignment(source_text: str, field_name: str, function_name: str) -> bool:
    return (
        re.search(
            rf"\.{re.escape(field_name)}\s*=\s*{re.escape(function_name)}\b",
            source_text,
        )
        is not None
    )


def has_renderer_family_mapping(
    client_renderer_text: str,
    renderer_family: str,
    ui_family: str,
) -> bool:
    pattern = re.compile(
        rf"case\s+{re.escape(renderer_family)}\s*:\s*"
        rf"return\s+{re.escape(ui_family)}\s*;",
        re.DOTALL,
    )
    return pattern.search(client_renderer_text) is not None


def suspicious_opengl_redirects(source_text: str) -> list[str]:
    patterns = {
        "vulkan family maps to OpenGL": (
            r"case\s+R_RENDERER_RMLUI_FAMILY_VULKAN\s*:\s*"
            r"return\s+UI_RML_RENDERER_FAMILY_OPENGL\s*;"
        ),
        "rtx/vkpt family maps to OpenGL": (
            r"case\s+R_RENDERER_RMLUI_FAMILY_RTX_VKPT\s*:\s*"
            r"return\s+UI_RML_RENDERER_FAMILY_OPENGL\s*;"
        ),
        "Vulkan renderer export claims OpenGL": (
            r"R_RENDERER_RMLUI_FAMILY_VULKAN[^;\n{}]*"
            r"R_RENDERER_RMLUI_FAMILY_OPENGL"
        ),
        "RTX/vkpt renderer export claims OpenGL": (
            r"R_RENDERER_RMLUI_FAMILY_RTX_VKPT[^;\n{}]*"
            r"R_RENDERER_RMLUI_FAMILY_OPENGL"
        ),
    }
    findings: list[str] = []
    for label, pattern in patterns.items():
        if re.search(pattern, source_text, re.DOTALL):
            findings.append(label)
    return findings


def add_fact(
    lane: BridgeReadinessLane,
    group: str,
    fact_name: str,
    value: bool,
    error: str,
) -> None:
    facts = lane.foundations if group == "foundations" else lane.guardrails
    facts[fact_name] = value
    if not value:
        lane.errors.append(f"{lane.lane_id}: {error}")


def append_missing_requirement(
    lane: BridgeReadinessLane,
    missing: bool,
    requirement: str,
) -> None:
    if missing:
        lane.missing_bridge_requirements.append(requirement)


def record_activation_requirement(
    lane: BridgeReadinessLane,
    requirement_id: str,
    satisfied: bool,
    requirement: str,
) -> None:
    lane.activation_requirements[requirement_id] = satisfied
    append_missing_requirement(lane, not satisfied, requirement)


def build_non_gl_api_facts(renderer_api_text: str) -> tuple[bool, bool]:
    non_gl_api_block = find_if_block(renderer_api_text, "USE_REF != REF_GL")
    non_gl_unavailable = (
        "Renderer_RmlUiCanRender" in non_gl_api_block
        and has_return(non_gl_api_block, "false")
        and "Renderer_RmlUiNativeRenderInterface" in non_gl_api_block
        and has_return(non_gl_api_block, "NULL")
    )
    fallback_exports = all(
        has_export_assignment(renderer_api_text, field_name, function_name)
        for field_name, function_name in (
            ("RmlUiRendererFamily", "R_RmlUiRendererFamily"),
            ("RmlUiRendererName", "R_RmlUiRendererName"),
            ("RmlUiCanRender", "Renderer_RmlUiCanRender"),
            ("RmlUiNativeRenderInterface", "Renderer_RmlUiNativeRenderInterface"),
        )
    )
    return non_gl_unavailable, fallback_exports


def meson_runtime_dependency_disabled_for_lane(meson_build_text: str, lane_id: str) -> bool:
    if lane_id == "vulkan":
        return (
            "renderer_vk_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" not in meson_build_text
            and "renderer_vk_deps += rmlui_dep" not in meson_build_text
        )
    return (
        "renderer_vk_rtx_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" not in meson_build_text
        and "renderer_vk_rtx_deps += rmlui_dep" not in meson_build_text
    )


def meson_runtime_enabled_for_lane(meson_build_text: str, lane_id: str) -> bool:
    if lane_id == "vulkan":
        return (
            "renderer_vk_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" in meson_build_text
            and "renderer_vk_deps += rmlui_dep" in meson_build_text
        )
    return (
        "renderer_vk_rtx_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" in meson_build_text
        and "renderer_vk_rtx_deps += rmlui_dep" in meson_build_text
    )


def meson_bridge_source_compiled_for_lane(meson_build_text: str, lane_id: str) -> bool:
    variable_prefix = "renderer_vk" if lane_id == "vulkan" else "renderer_vk_rtx"
    source_path = "src/renderer/rmlui_bridge.cpp"

    def variable_mentions_source(variable_name: str) -> bool:
        source_pattern = rf"['\"]{re.escape(source_path)}['\"]"
        line_match = re.search(
            rf"\b{re.escape(variable_name)}\s*(?:\+?=|=).*{source_pattern}",
            meson_build_text,
        )
        if line_match:
            return True

        list_match = re.search(
            rf"\b{re.escape(variable_name)}\s*(?:\+?=|=)\s*\[(?P<body>.*?)\]",
            meson_build_text,
            re.DOTALL,
        )
        return bool(list_match and source_path in list_match.group("body"))

    return (
        variable_mentions_source(f"{variable_prefix}_src")
        or variable_mentions_source(f"{variable_prefix}_lib_src")
        or (
            re.search(
                rf"\b{re.escape(variable_prefix)}_lib_src\s*=\s*{re.escape(variable_prefix)}_src\b",
                meson_build_text,
            ) is not None
            and variable_mentions_source(f"{variable_prefix}_src")
        )
        or f"{variable_prefix}_lib_src = renderer_src" in meson_build_text
    )


def renderer_bridge_class_missing(renderer_bridge_text: str, lane_id: str) -> bool:
    if lane_id == "vulkan":
        return (
            "R_RmlUiVulkanRenderInterface" not in renderer_bridge_text
            and "R_RmlUiVkRenderInterface" not in renderer_bridge_text
        )
    return (
        "R_RmlUiRtxVkptRenderInterface" not in renderer_bridge_text
        and "R_RmlUiVkptRenderInterface" not in renderer_bridge_text
    )


def native_family_claim_missing(
    renderer_api_text: str,
    renderer_bridge_text: str,
    lane_id: str,
) -> bool:
    family = (
        "R_RENDERER_RMLUI_FAMILY_VULKAN"
        if lane_id == "vulkan"
        else "R_RENDERER_RMLUI_FAMILY_RTX_VKPT"
    )
    return family not in renderer_api_text and family not in renderer_bridge_text


def validate_vulkan_lane(
    lane: BridgeReadinessLane,
    texts: dict[str, str],
    non_gl_unavailable: bool,
    fallback_exports: bool,
    no_opengl_redirect: bool,
) -> None:
    renderer_header_text = texts["renderer_header"]
    renderer_api_text = texts["renderer_api_source"]
    renderer_bridge_text = texts["renderer_bridge_source"]
    client_renderer_text = texts["client_renderer_source"]
    meson_build_text = texts["meson_build"]
    vk_ui_header_text = texts["vulkan_ui_header"]
    vk_ui_source_text = texts["vulkan_ui_source"]
    vk_main_text = texts["vulkan_main_source"]

    add_fact(
        lane,
        "foundations",
        "draw_entrypoints_present",
        has_all_tokens(
            vk_ui_header_text,
            (
                "VK_UI_DrawPic",
                "VK_UI_DrawStretchPic",
                "VK_UI_DrawStretchSubPic",
            ),
        )
        and has_all_tokens(vk_main_text, ("R_DrawPic", "R_DrawStretchPic", "VK_UI_DrawStretchPic")),
        "native Vulkan UI draw entrypoints must remain available for a future RmlUi bridge",
    )
    add_fact(
        lane,
        "foundations",
        "frame_recording_present",
        has_all_tokens(vk_ui_header_text, ("VK_UI_BeginFrame", "VK_UI_EndFrame", "VK_UI_Record"))
        and has_all_tokens(vk_main_text, ("VK_UI_BeginFrame", "VK_UI_EndFrame", "VK_UI_Record")),
        "native Vulkan UI frame recording must remain available for a future RmlUi bridge",
    )
    add_fact(
        lane,
        "foundations",
        "texture_upload_present",
        has_all_tokens(
            vk_ui_header_text + "\n" + vk_ui_source_text,
            (
                "VK_UI_RegisterRawImage",
                "VK_UI_UpdateImageRGBA",
                "VK_UI_UpdateImageRGBASubRect",
            ),
        )
        and has_all_tokens(vk_main_text, ("R_RegisterRawImage", "R_UpdateImageRGBA")),
        "native Vulkan texture upload/update primitives must remain available",
    )
    add_fact(
        lane,
        "foundations",
        "descriptor_surface_present",
        has_all_tokens(
            vk_ui_header_text + "\n" + vk_ui_source_text,
            (
                "VkDescriptorSet",
                "VK_UI_GetDescriptorSetForImage",
                "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER",
            ),
        ),
        "native Vulkan descriptor-set surface must remain available",
    )
    add_fact(
        lane,
        "foundations",
        "clip_surface_present",
        has_all_tokens(vk_ui_header_text + "\n" + vk_ui_source_text, ("VK_UI_SetClipRect", "vkCmdSetScissor"))
        and has_all_tokens(vk_main_text, ("R_SetClipRect", "VK_UI_SetClipRect")),
        "native Vulkan clip/scissor surface must remain available",
    )

    class_missing = renderer_bridge_class_missing(renderer_bridge_text, lane.lane_id)
    source_missing = not meson_bridge_source_compiled_for_lane(meson_build_text, lane.lane_id)
    family_missing = native_family_claim_missing(renderer_api_text, renderer_bridge_text, lane.lane_id)
    runtime_enabled = meson_runtime_enabled_for_lane(meson_build_text, lane.lane_id)
    runtime_dependency_disabled = meson_runtime_dependency_disabled_for_lane(meson_build_text, lane.lane_id)
    native_interface_exported = (
        not class_missing
        and not source_missing
        and not family_missing
        and runtime_enabled
        and not (non_gl_unavailable and fallback_exports)
    )
    class_inactive = (
        class_missing
        or (
            not native_interface_exported
            and non_gl_unavailable
            and fallback_exports
        )
    )
    family_inactive = (
        family_missing
        or (
            not class_missing
            and not source_missing
            and not native_interface_exported
            and non_gl_unavailable
            and fallback_exports
        )
    )
    runtime_dependency_inactive = (
        runtime_dependency_disabled
        or (
            runtime_enabled
            and not native_interface_exported
            and non_gl_unavailable
            and fallback_exports
        )
    )
    record_activation_requirement(
        lane,
        "native_bridge_class_present",
        not class_missing,
        "renderer-owned Vulkan Rml::RenderInterface class",
    )
    record_activation_requirement(
        lane,
        "native_bridge_source_compiled",
        not source_missing,
        "Vulkan renderer build includes native RmlUi bridge source",
    )
    record_activation_requirement(
        lane,
        "native_family_export_present",
        not family_missing,
        "Vulkan renderer-family export",
    )
    record_activation_requirement(
        lane,
        "runtime_dependency_enabled",
        runtime_enabled,
        "Vulkan RmlUi runtime dependency",
    )
    record_activation_requirement(
        lane,
        "native_interface_export_present",
        native_interface_exported,
        "non-null Vulkan native render-interface export",
    )

    add_fact(
        lane,
        "guardrails",
        "family_declared",
        has_token(renderer_header_text, "R_RENDERER_RMLUI_FAMILY_VULKAN"),
        "renderer header must keep a distinct Vulkan RmlUi family lane",
    )
    add_fact(
        lane,
        "guardrails",
        "client_maps_family_distinctly",
        has_renderer_family_mapping(
            client_renderer_text,
            "R_RENDERER_RMLUI_FAMILY_VULKAN",
            "UI_RML_RENDERER_FAMILY_VULKAN",
        ),
        "client renderer must map the Vulkan family to the Vulkan UI lane",
    )
    add_fact(
        lane,
        "guardrails",
        "renderer_api_non_gl_unavailable",
        non_gl_unavailable and fallback_exports,
        "non-OpenGL renderer exports must identify the lane while keeping CanRender=false and the native interface NULL",
    )
    add_fact(
        lane,
        "guardrails",
        "runtime_dependency_inactive",
        runtime_dependency_inactive,
        "Vulkan RmlUi runtime dependency must stay inactive until a native interface export exists",
    )
    add_fact(
        lane,
        "guardrails",
        "native_bridge_class_inactive",
        class_inactive,
        "Vulkan RmlUi bridge class must stay inactive until family, runtime, and interface exports are wired",
    )
    add_fact(
        lane,
        "guardrails",
        "native_family_export_inactive",
        family_inactive,
        "Vulkan RmlUi family export must stay inactive until bridge source, runtime, and interface exports are wired",
    )
    add_fact(
        lane,
        "guardrails",
        "not_redirected_to_opengl",
        no_opengl_redirect,
        "Vulkan lane must not redirect through the OpenGL RmlUi bridge",
    )


def validate_rtx_lane(
    lane: BridgeReadinessLane,
    texts: dict[str, str],
    non_gl_unavailable: bool,
    fallback_exports: bool,
    no_opengl_redirect: bool,
) -> None:
    renderer_header_text = texts["renderer_header"]
    renderer_api_text = texts["renderer_api_source"]
    renderer_bridge_text = texts["renderer_bridge_source"]
    client_renderer_text = texts["client_renderer_source"]
    meson_build_text = texts["meson_build"]
    rtx_header_text = texts["rtx_header"]
    rtx_draw_text = texts["rtx_draw_source"]
    rtx_main_text = texts["rtx_main_source"]
    rtx_images_text = texts["rtx_images_source"]
    rtx_textures_text = texts["rtx_textures_source"]
    rtx_vert_text = texts["rtx_stretch_pic_vertex_shader"]
    rtx_frag_text = texts["rtx_stretch_pic_fragment_shader"]

    add_fact(
        lane,
        "foundations",
        "draw_entrypoints_present",
        has_all_tokens(rtx_header_text, ("R_DrawPic", "R_DrawStretchPic"))
        and has_all_tokens(rtx_draw_text, ("R_DrawPic", "R_DrawStretchPic", "enqueue_stretch_pic")),
        "native RTX/vkpt UI draw entrypoints must remain available for a future RmlUi bridge",
    )
    add_fact(
        lane,
        "foundations",
        "stretch_pic_submit_present",
        has_all_tokens(
            rtx_draw_text + "\n" + rtx_header_text,
            ("vkpt_draw_clear_stretch_pics", "vkpt_draw_submit_stretch_pics"),
        )
        and has_all_tokens(rtx_main_text, ("vkpt_draw_clear_stretch_pics", "vkpt_draw_submit_stretch_pics")),
        "native RTX/vkpt stretch-pic submit path must remain available",
    )
    add_fact(
        lane,
        "foundations",
        "texture_upload_present",
        has_all_tokens(rtx_images_text, ("R_RegisterImage", "R_RegisterRawImage"))
        and has_all_tokens(rtx_textures_text + "\n" + rtx_header_text, ("IMG_Load_RTX", "R_UpdateImageRGBA")),
        "native RTX/vkpt image registration and texture update primitives must remain available",
    )
    add_fact(
        lane,
        "foundations",
        "descriptor_texture_surface_present",
        has_all_tokens(
            rtx_textures_text + "\n" + rtx_frag_text,
            (
                "qvk_get_current_desc_set_textures",
                "vkpt_textures_update_descriptor_set",
                "global_textureLod",
            ),
        ),
        "native RTX/vkpt descriptor texture surface must remain available",
    )
    add_fact(
        lane,
        "foundations",
        "clip_surface_present",
        has_all_tokens(rtx_draw_text + "\n" + rtx_header_text, ("R_SetClipRect", "clip_enable", "clip_rect")),
        "native RTX/vkpt clip surface must remain available",
    )
    add_fact(
        lane,
        "foundations",
        "stretch_pic_shader_present",
        has_all_tokens(rtx_vert_text, ("struct StretchPic", "stretch_pics", "gl_InstanceIndex"))
        and has_all_tokens(rtx_frag_text, ("global_textureLod", "outColor")),
        "native RTX/vkpt stretch-pic shaders must remain available",
    )

    class_missing = renderer_bridge_class_missing(renderer_bridge_text, lane.lane_id)
    source_missing = not meson_bridge_source_compiled_for_lane(meson_build_text, lane.lane_id)
    family_missing = native_family_claim_missing(renderer_api_text, renderer_bridge_text, lane.lane_id)
    runtime_enabled = meson_runtime_enabled_for_lane(meson_build_text, lane.lane_id)
    runtime_dependency_disabled = meson_runtime_dependency_disabled_for_lane(meson_build_text, lane.lane_id)
    native_interface_exported = (
        not class_missing
        and not source_missing
        and not family_missing
        and runtime_enabled
        and not (non_gl_unavailable and fallback_exports)
    )
    class_inactive = (
        class_missing
        or (
            not native_interface_exported
            and non_gl_unavailable
            and fallback_exports
        )
    )
    family_inactive = (
        family_missing
        or (
            not class_missing
            and not source_missing
            and not native_interface_exported
            and non_gl_unavailable
            and fallback_exports
        )
    )
    runtime_dependency_inactive = (
        runtime_dependency_disabled
        or (
            runtime_enabled
            and not native_interface_exported
            and non_gl_unavailable
            and fallback_exports
        )
    )
    record_activation_requirement(
        lane,
        "native_bridge_class_present",
        not class_missing,
        "renderer-owned RTX/vkpt Rml::RenderInterface class",
    )
    record_activation_requirement(
        lane,
        "native_bridge_source_compiled",
        not source_missing,
        "RTX/vkpt renderer build includes native RmlUi bridge source",
    )
    record_activation_requirement(
        lane,
        "native_family_export_present",
        not family_missing,
        "RTX/vkpt renderer-family export",
    )
    record_activation_requirement(
        lane,
        "runtime_dependency_enabled",
        runtime_enabled,
        "RTX/vkpt RmlUi runtime dependency",
    )
    record_activation_requirement(
        lane,
        "native_interface_export_present",
        native_interface_exported,
        "non-null RTX/vkpt native render-interface export",
    )

    add_fact(
        lane,
        "guardrails",
        "family_declared",
        has_token(renderer_header_text, "R_RENDERER_RMLUI_FAMILY_RTX_VKPT"),
        "renderer header must keep a distinct RTX/vkpt RmlUi family lane",
    )
    add_fact(
        lane,
        "guardrails",
        "client_maps_family_distinctly",
        has_renderer_family_mapping(
            client_renderer_text,
            "R_RENDERER_RMLUI_FAMILY_RTX_VKPT",
            "UI_RML_RENDERER_FAMILY_RTX_VKPT",
        ),
        "client renderer must map the RTX/vkpt family to the RTX/vkpt UI lane",
    )
    add_fact(
        lane,
        "guardrails",
        "renderer_api_non_gl_unavailable",
        non_gl_unavailable and fallback_exports,
        "non-OpenGL renderer exports must identify the lane while keeping CanRender=false and the native interface NULL",
    )
    add_fact(
        lane,
        "guardrails",
        "runtime_dependency_inactive",
        runtime_dependency_inactive,
        "RTX/vkpt RmlUi runtime dependency must stay inactive until a native interface export exists",
    )
    add_fact(
        lane,
        "guardrails",
        "native_bridge_class_inactive",
        class_inactive,
        "RTX/vkpt RmlUi bridge class must stay inactive until family, runtime, and interface exports are wired",
    )
    add_fact(
        lane,
        "guardrails",
        "native_family_export_inactive",
        family_inactive,
        "RTX/vkpt RmlUi family export must stay inactive until bridge source, runtime, and interface exports are wired",
    )
    add_fact(
        lane,
        "guardrails",
        "not_redirected_to_opengl",
        no_opengl_redirect,
        "RTX/vkpt lane must not redirect through the OpenGL RmlUi bridge",
    )


def validate_bridge_readiness(repo_root: Path, paths: dict[str, Path]) -> BridgeReadinessReport:
    repo_root = repo_root.resolve()
    resolved_paths = {
        label: resolve_path(path, repo_root)
        for label, path in paths.items()
    }
    texts = {
        label: read_text_if_file(path)
        for label, path in resolved_paths.items()
    }

    report = BridgeReadinessReport(
        repo_root=repo_root,
        paths=resolved_paths,
        files={
            label: path.is_file()
            for label, path in resolved_paths.items()
        },
    )

    for file_label, exists in report.files.items():
        if not exists:
            report.errors.append(f"missing {file_label} input")

    renderer_header_text = texts["renderer_header"]
    renderer_api_text = texts["renderer_api_source"]
    renderer_bridge_text = texts["renderer_bridge_source"]
    client_renderer_text = texts["client_renderer_source"]
    non_gl_unavailable, fallback_exports = build_non_gl_api_facts(renderer_api_text)
    no_opengl_redirect = not suspicious_opengl_redirects(
        "\n".join((renderer_api_text, renderer_bridge_text, client_renderer_text))
    )

    report.contract_facts["renderer_non_gl_families_declared"] = all(
        has_token(renderer_header_text, family)
        for family in (
            "R_RENDERER_RMLUI_FAMILY_VULKAN",
            "R_RENDERER_RMLUI_FAMILY_RTX_VKPT",
        )
    )
    report.contract_facts["client_non_gl_families_are_distinct"] = (
        has_renderer_family_mapping(
            client_renderer_text,
            "R_RENDERER_RMLUI_FAMILY_VULKAN",
            "UI_RML_RENDERER_FAMILY_VULKAN",
        )
        and has_renderer_family_mapping(
            client_renderer_text,
            "R_RENDERER_RMLUI_FAMILY_RTX_VKPT",
            "UI_RML_RENDERER_FAMILY_RTX_VKPT",
        )
    )
    report.contract_facts["renderer_api_non_gl_unavailable"] = (
        non_gl_unavailable and fallback_exports
    )
    report.contract_facts["no_vulkan_or_rtx_to_opengl_redirect"] = no_opengl_redirect

    for fact_name, error in (
        ("renderer_non_gl_families_declared", "renderer header must declare distinct Vulkan and RTX/vkpt RmlUi families"),
        ("client_non_gl_families_are_distinct", "client renderer must keep Vulkan and RTX/vkpt UI families distinct"),
        ("renderer_api_non_gl_unavailable", "non-OpenGL renderer exports must remain unavailable until native bridges exist"),
        ("no_vulkan_or_rtx_to_opengl_redirect", "Vulkan/RTX RmlUi lanes must not map to OpenGL"),
    ):
        if not report.contract_facts[fact_name]:
            report.errors.append(error)

    vulkan_lane = BridgeReadinessLane("vulkan", LANE_LABELS["vulkan"])
    validate_vulkan_lane(
        vulkan_lane,
        texts,
        non_gl_unavailable,
        fallback_exports,
        no_opengl_redirect,
    )
    report.lanes[vulkan_lane.lane_id] = vulkan_lane

    rtx_lane = BridgeReadinessLane("rtx_vkpt", LANE_LABELS["rtx_vkpt"])
    validate_rtx_lane(
        rtx_lane,
        texts,
        non_gl_unavailable,
        fallback_exports,
        no_opengl_redirect,
    )
    report.lanes[rtx_lane.lane_id] = rtx_lane

    return report


def lane_payload(lane: BridgeReadinessLane) -> dict[str, Any]:
    return {
        "ok": lane.ok(),
        "label": lane.label,
        "expected_status": lane.expected_status,
        "foundation_ok": lane.foundation_ok(),
        "native_bridge_claimed": lane.native_bridge_claimed(),
        "activation_status": lane.activation_status(),
        "activation_complete": lane.activation_complete(),
        "foundations": lane.foundations,
        "guardrails": lane.guardrails,
        "activation_requirements": lane.activation_requirements,
        "satisfied_activation_requirement_ids": lane.satisfied_activation_requirement_ids(),
        "pending_activation_requirement_ids": lane.pending_activation_requirement_ids(),
        "next_activation_requirement": lane.next_activation_requirement(),
        "missing_bridge_requirements": lane.missing_bridge_requirements,
        "errors": lane.errors,
    }


def json_report_payload(report: BridgeReadinessReport) -> dict[str, Any]:
    lane_payloads = {
        lane_id: lane_payload(lane)
        for lane_id, lane in report.lanes.items()
    }
    return {
        "ok": report.ok(),
        "paths": {
            "repo_root": str(report.repo_root),
            **{
                label: display_path(path, report.repo_root)
                for label, path in report.paths.items()
            },
        },
        "files": report.files,
        "contract": report.contract_facts,
        "lanes": lane_payloads,
        "counts": {
            "lanes": len(report.lanes),
            "foundation_lanes": sum(
                1
                for lane in report.lanes.values()
                if lane.foundation_ok()
            ),
            "native_bridge_lanes": sum(
                1
                for lane in report.lanes.values()
                if lane.native_bridge_claimed()
            ),
            "blocked_lanes": sum(
                1
                for lane in report.lanes.values()
                if lane.expected_status == "blocked_until_native"
            ),
            "activation_complete_lanes": sum(
                1
                for lane in report.lanes.values()
                if lane.activation_complete()
            ),
            "partial_activation_lanes": sum(
                1
                for lane in report.lanes.values()
                if lane.partial_activation_claimed()
            ),
            "inactive_activation_lanes": sum(
                1
                for lane in report.lanes.values()
                if lane.activation_status() == "blocked_no_activation"
            ),
            "activation_requirements": sum(
                lane.activation_requirement_count()
                for lane in report.lanes.values()
            ),
            "satisfied_activation_requirements": sum(
                lane.satisfied_activation_requirement_count()
                for lane in report.lanes.values()
            ),
            "pending_activation_requirements": sum(
                lane.pending_activation_requirement_count()
                for lane in report.lanes.values()
            ),
            "missing_bridge_requirements": sum(
                len(lane.missing_bridge_requirements)
                for lane in report.lanes.values()
            ),
            "errors": len(report.all_errors()),
        },
        "errors": report.all_errors(),
    }


def print_json_report(report: BridgeReadinessReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def print_lane_report(lane: BridgeReadinessLane) -> None:
    print(f"  {lane.label} ({lane.expected_status}): {'pass' if lane.ok() else 'fail'}")
    print(f"    foundation_ok: {yes_no(lane.foundation_ok())}")
    print(f"    native_bridge_claimed: {yes_no(lane.native_bridge_claimed())}")
    print(f"    activation_status: {lane.activation_status()}")
    next_requirement = lane.next_activation_requirement()
    print(f"    next_activation_requirement: {next_requirement or '-'}")
    print("    Foundations:")
    for fact_name, value in lane.foundations.items():
        print(f"      {fact_name}: {yes_no(value)}")
    print("    Guardrails:")
    for fact_name, value in lane.guardrails.items():
        print(f"      {fact_name}: {yes_no(value)}")
    print("    Activation requirements:")
    for requirement_name, value in lane.activation_requirements.items():
        print(f"      {requirement_name}: {yes_no(value)}")
    print("    Missing bridge requirements:")
    for requirement in lane.missing_bridge_requirements:
        print(f"      - {requirement}")


def print_report(report: BridgeReadinessReport) -> None:
    print("RmlUi Vulkan/RTX bridge-readiness audit:")
    print(f"  Malformed findings: {len(report.all_errors())}")
    print(f"  Repo root: {report.repo_root}")

    print("\nInputs:")
    for label, path in report.paths.items():
        print(f"  {label}: {display_path(path, report.repo_root)}")

    print("\nContract facts:")
    for fact_name, value in report.contract_facts.items():
        print(f"  {fact_name}: {yes_no(value)}")

    print("\nReadiness lanes:")
    for lane in report.lanes.values():
        print_lane_report(lane)

    errors = report.all_errors()
    if errors:
        print("\nErrors:")
        for error in errors:
            print(f"  - {error}")
        print("\nResult: RmlUi Vulkan/RTX bridge-readiness audit failed.")
    else:
        print("\nResult: RmlUi Vulkan/RTX bridge-readiness audit passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve default scan paths.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format. Defaults to the text report.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    report = validate_bridge_readiness(repo_root, DEFAULT_INPUTS)

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
