#!/usr/bin/env python3
"""Validate the guarded RmlUi renderer-family matrix contract."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_MESON_BUILD = Path("meson.build")
DEFAULT_RENDERER_HEADER = Path("inc/renderer/renderer.h")
DEFAULT_RENDERER_API_SOURCE = Path("src/renderer/renderer_api.c")
DEFAULT_RENDERER_BRIDGE_SOURCE = Path("src/renderer/rmlui_bridge.cpp")
DEFAULT_CLIENT_RENDERER_SOURCE = Path("src/client/renderer.cpp")
DEFAULT_CAPTURE_CHECKER = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

LANE_LABELS = {
    "opengl": "OpenGL",
    "vulkan": "Vulkan",
    "rtx_vkpt": "RTX/vkpt",
}


@dataclass
class RendererLaneReport:
    lane_id: str
    label: str
    expected_status: str
    facts: dict[str, bool] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


@dataclass
class RendererMatrixReport:
    repo_root: Path
    meson_build_path: Path
    renderer_header_path: Path
    renderer_api_source_path: Path
    renderer_bridge_source_path: Path
    client_renderer_source_path: Path
    capture_checker_path: Path
    files: dict[str, bool] = field(default_factory=dict)
    contract_facts: dict[str, bool] = field(default_factory=dict)
    lanes: dict[str, RendererLaneReport] = field(default_factory=dict)
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


def find_if_block(source_text: str, directive: str) -> str:
    pattern = re.compile(
        rf"#if\s+{re.escape(directive)}(?P<body>.*?)(?:#else.*?)*#endif",
        re.DOTALL,
    )
    match = pattern.search(source_text)
    if not match:
        return ""
    return match.group("body")


def has_token(source_text: str, token: str) -> bool:
    return re.search(rf"\b{re.escape(token)}\b", source_text) is not None


def has_export_assignment(source_text: str, field_name: str, function_name: str) -> bool:
    return (
        re.search(
            rf"\.{re.escape(field_name)}\s*=\s*{re.escape(function_name)}\b",
            source_text,
        )
        is not None
    )


def has_return(source_text: str, value: str) -> bool:
    return re.search(rf"\breturn\s+{re.escape(value)}\s*;", source_text) is not None


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


def validate_lane_fact(
    lane: RendererLaneReport,
    fact_name: str,
    value: bool,
    error: str,
) -> None:
    lane.facts[fact_name] = value
    if not value:
        lane.errors.append(f"{lane.lane_id}: {error}")


def validate_renderer_matrix(
    repo_root: Path,
    meson_build_path: Path,
    renderer_header_path: Path,
    renderer_api_source_path: Path,
    renderer_bridge_source_path: Path,
    client_renderer_source_path: Path,
    capture_checker_path: Path,
) -> RendererMatrixReport:
    repo_root = repo_root.resolve()
    meson_build_path = meson_build_path.resolve(strict=False)
    renderer_header_path = renderer_header_path.resolve(strict=False)
    renderer_api_source_path = renderer_api_source_path.resolve(strict=False)
    renderer_bridge_source_path = renderer_bridge_source_path.resolve(strict=False)
    client_renderer_source_path = client_renderer_source_path.resolve(strict=False)
    capture_checker_path = capture_checker_path.resolve(strict=False)

    meson_build_text = read_text_if_file(meson_build_path)
    renderer_header_text = read_text_if_file(renderer_header_path)
    renderer_api_text = read_text_if_file(renderer_api_source_path)
    renderer_bridge_text = read_text_if_file(renderer_bridge_source_path)
    client_renderer_text = read_text_if_file(client_renderer_source_path)
    capture_checker_text = read_text_if_file(capture_checker_path)

    report = RendererMatrixReport(
        repo_root=repo_root,
        meson_build_path=meson_build_path,
        renderer_header_path=renderer_header_path,
        renderer_api_source_path=renderer_api_source_path,
        renderer_bridge_source_path=renderer_bridge_source_path,
        client_renderer_source_path=client_renderer_source_path,
        capture_checker_path=capture_checker_path,
        files={
            "meson_build": meson_build_path.is_file(),
            "renderer_header": renderer_header_path.is_file(),
            "renderer_api_source": renderer_api_source_path.is_file(),
            "renderer_bridge_source": renderer_bridge_source_path.is_file(),
            "client_renderer_source": client_renderer_source_path.is_file(),
            "capture_checker": capture_checker_path.is_file(),
        },
    )

    for file_label, exists in report.files.items():
        if not exists:
            report.errors.append(f"missing {file_label} input")

    required_renderer_families = (
        "R_RENDERER_RMLUI_FAMILY_OPENGL",
        "R_RENDERER_RMLUI_FAMILY_VULKAN",
        "R_RENDERER_RMLUI_FAMILY_RTX_VKPT",
    )
    report.contract_facts["renderer_families_declared"] = all(
        has_token(renderer_header_text, family)
        for family in required_renderer_families
    )
    report.contract_facts["renderer_exports_declared"] = all(
        token in renderer_header_text
        for token in (
            "R_RmlUiRendererFamily",
            "R_RmlUiRendererName",
            "R_RmlUiCanRender",
            "R_RmlUiNativeRenderInterface",
            "RmlUiRendererFamily",
            "RmlUiRendererName",
            "RmlUiCanRender",
            "RmlUiNativeRenderInterface",
        )
    )
    report.contract_facts["client_clears_none_family"] = (
        "renderer.family == UI_RML_RENDERER_FAMILY_NONE" in client_renderer_text
        and "UI_Rml_ClearRendererInterface();" in client_renderer_text
    )
    report.contract_facts["client_registers_renderer_bridge"] = (
        "RmlUi_RegisterRendererBridge();" in client_renderer_text
        and "UI_Rml_SetRendererInterface(&renderer)" in client_renderer_text
    )
    report.contract_facts["no_vulkan_or_rtx_to_opengl_redirect"] = not suspicious_opengl_redirects(
        "\n".join(
            (
                renderer_api_text,
                renderer_bridge_text,
                client_renderer_text,
            )
        )
    )

    for fact_name, error in (
        ("renderer_families_declared", "renderer header must declare OpenGL, Vulkan, and RTX/vkpt RmlUi families"),
        ("renderer_exports_declared", "renderer header must declare the RmlUi export hooks"),
        ("client_clears_none_family", "client renderer must clear RmlUi when no native family is registered"),
        ("client_registers_renderer_bridge", "client renderer must register the RmlUi renderer bridge"),
        ("no_vulkan_or_rtx_to_opengl_redirect", "Vulkan/RTX RmlUi lanes must not map to OpenGL"),
    ):
        if not report.contract_facts[fact_name]:
            report.errors.append(error)

    non_gl_api_block = find_if_block(renderer_api_text, "USE_REF != REF_GL")
    gl_export_block = find_if_block(renderer_api_text, "USE_REF == REF_GL")

    api_non_gl_unavailable = (
        "Renderer_RmlUiCanRender" in non_gl_api_block
        and has_return(non_gl_api_block, "false")
        and "Renderer_RmlUiNativeRenderInterface" in non_gl_api_block
        and has_return(non_gl_api_block, "NULL")
    )
    api_gl_exports_native_hooks = (
        all(
            has_export_assignment(renderer_api_text, field_name, function_name)
            for field_name, function_name in (
                ("RmlUiRendererFamily", "R_RmlUiRendererFamily"),
                ("RmlUiRendererName", "R_RmlUiRendererName"),
            )
        )
        and all(
            has_export_assignment(gl_export_block, field_name, function_name)
            for field_name, function_name in (
                ("RmlUiCanRender", "R_RmlUiCanRender"),
                ("RmlUiNativeRenderInterface", "R_RmlUiNativeRenderInterface"),
            )
        )
    )
    api_else_exports_unavailable_hooks = all(
        has_export_assignment(renderer_api_text, field_name, function_name)
        for field_name, function_name in (
            ("RmlUiRendererFamily", "R_RmlUiRendererFamily"),
            ("RmlUiRendererName", "R_RmlUiRendererName"),
            ("RmlUiCanRender", "Renderer_RmlUiCanRender"),
            ("RmlUiNativeRenderInterface", "Renderer_RmlUiNativeRenderInterface"),
        )
    )

    meson_gl_runtime_enabled = (
        "renderer_gl_deps += rmlui_dep" in meson_build_text
        and "renderer_gl_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" in meson_build_text
    )
    meson_vk_runtime_disabled = (
        "renderer_vk_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" not in meson_build_text
        and "renderer_vk_deps += rmlui_dep" not in meson_build_text
    )
    meson_vk_runtime_enabled = (
        "renderer_vk_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" in meson_build_text
        and "renderer_vk_deps += rmlui_dep" in meson_build_text
    )
    meson_rtx_runtime_disabled = (
        "renderer_vk_rtx_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" not in meson_build_text
        and "renderer_vk_rtx_deps += rmlui_dep" not in meson_build_text
    )
    meson_rtx_runtime_enabled = (
        "renderer_vk_rtx_cpp_args += '-DUI_RML_HAS_RUNTIME=1'" in meson_build_text
        and "renderer_vk_rtx_deps += rmlui_dep" in meson_build_text
    )
    capture_harness_opengl_only = (
        '"r_renderer"' in capture_checker_text
        and '"opengl"' in capture_checker_text
        and "DEFAULT_ROUTE_MATRIX" in capture_checker_text
        and '"vulkan"' not in capture_checker_text.lower()
        and '"rtx"' not in capture_checker_text.lower()
    )

    opengl_lane = RendererLaneReport(
        lane_id="opengl",
        label=LANE_LABELS["opengl"],
        expected_status="native_guarded",
    )
    validate_lane_fact(
        opengl_lane,
        "meson_runtime_enabled_for_opengl",
        meson_gl_runtime_enabled,
        "Meson must enable the RmlUi runtime dependency for the OpenGL renderer lane",
    )
    validate_lane_fact(
        opengl_lane,
        "renderer_api_exports_native_hooks",
        api_gl_exports_native_hooks,
        "renderer API must export native RmlUi hooks in the REF_GL lane",
    )
    validate_lane_fact(
        opengl_lane,
        "native_render_interface_class_present",
        "class R_RmlUiOpenGLRenderInterface final : public Rml::RenderInterface"
        in renderer_bridge_text,
        "OpenGL lane must own a concrete Rml::RenderInterface",
    )
    validate_lane_fact(
        opengl_lane,
        "render_methods_present",
        all(
            method in renderer_bridge_text
            for method in (
                "CompileGeometry",
                "RenderGeometry",
                "ReleaseGeometry",
                "LoadTexture",
                "GenerateTexture",
                "ReleaseTexture",
                "EnableScissorRegion",
                "SetScissorRegion",
            )
        ),
        "OpenGL native bridge must provide geometry, texture, and scissor methods",
    )
    validate_lane_fact(
        opengl_lane,
        "family_reports_opengl",
        has_return(renderer_bridge_text, "R_RENDERER_RMLUI_FAMILY_OPENGL"),
        "OpenGL native bridge must report the OpenGL RmlUi family",
    )
    validate_lane_fact(
        opengl_lane,
        "can_render_true",
        "R_RmlUiCanRender" in renderer_bridge_text
        and has_return(renderer_bridge_text, "true"),
        "OpenGL native bridge must report CanRender=true when the runtime is compiled",
    )
    validate_lane_fact(
        opengl_lane,
        "native_render_interface_returned",
        "return &r_rmlui_opengl_render_interface;" in renderer_bridge_text,
        "OpenGL native bridge must return its Rml::RenderInterface instance",
    )
    validate_lane_fact(
        opengl_lane,
        "capture_harness_uses_opengl",
        capture_harness_opengl_only,
        "guarded runtime capture matrix must remain explicitly OpenGL-scoped",
    )
    report.lanes["opengl"] = opengl_lane

    vulkan_lane = RendererLaneReport(
        lane_id="vulkan",
        label=LANE_LABELS["vulkan"],
        expected_status="blocked_until_native",
    )
    validate_lane_fact(
        vulkan_lane,
        "family_declared",
        has_token(renderer_header_text, "R_RENDERER_RMLUI_FAMILY_VULKAN"),
        "renderer header must keep a distinct Vulkan family lane",
    )
    validate_lane_fact(
        vulkan_lane,
        "client_maps_family_distinctly",
        has_renderer_family_mapping(
            client_renderer_text,
            "R_RENDERER_RMLUI_FAMILY_VULKAN",
            "UI_RML_RENDERER_FAMILY_VULKAN",
        ),
        "client renderer must map the Vulkan family to the Vulkan UI lane",
    )
    validate_lane_fact(
        vulkan_lane,
        "renderer_api_non_gl_unavailable",
        api_non_gl_unavailable and api_else_exports_unavailable_hooks,
        "non-OpenGL renderer exports must identify the lane while keeping CanRender=false and the native interface NULL",
    )
    validate_lane_fact(
        vulkan_lane,
        "runtime_dependency_inactive",
        meson_vk_runtime_disabled
        or (
            meson_vk_runtime_enabled
            and api_non_gl_unavailable
            and api_else_exports_unavailable_hooks
        ),
        "Vulkan runtime dependency must stay inactive until native rendering is available",
    )
    validate_lane_fact(
        vulkan_lane,
        "native_family_export_inactive",
        (
            "R_RENDERER_RMLUI_FAMILY_VULKAN" not in renderer_bridge_text
            and "R_RENDERER_RMLUI_FAMILY_VULKAN" not in renderer_api_text
        )
        or (
            api_non_gl_unavailable
            and api_else_exports_unavailable_hooks
            and (meson_vk_runtime_disabled or meson_vk_runtime_enabled)
        ),
        "Vulkan family export must stay inactive until native rendering is available",
    )
    validate_lane_fact(
        vulkan_lane,
        "not_redirected_to_opengl",
        report.contract_facts["no_vulkan_or_rtx_to_opengl_redirect"],
        "Vulkan lane must not redirect through the OpenGL RmlUi bridge",
    )
    report.lanes["vulkan"] = vulkan_lane

    rtx_lane = RendererLaneReport(
        lane_id="rtx_vkpt",
        label=LANE_LABELS["rtx_vkpt"],
        expected_status="blocked_until_native",
    )
    validate_lane_fact(
        rtx_lane,
        "family_declared",
        has_token(renderer_header_text, "R_RENDERER_RMLUI_FAMILY_RTX_VKPT"),
        "renderer header must keep a distinct RTX/vkpt family lane",
    )
    validate_lane_fact(
        rtx_lane,
        "client_maps_family_distinctly",
        has_renderer_family_mapping(
            client_renderer_text,
            "R_RENDERER_RMLUI_FAMILY_RTX_VKPT",
            "UI_RML_RENDERER_FAMILY_RTX_VKPT",
        ),
        "client renderer must map the RTX/vkpt family to the RTX/vkpt UI lane",
    )
    validate_lane_fact(
        rtx_lane,
        "renderer_api_non_gl_unavailable",
        api_non_gl_unavailable and api_else_exports_unavailable_hooks,
        "non-OpenGL renderer exports must identify the lane while keeping CanRender=false and the native interface NULL",
    )
    validate_lane_fact(
        rtx_lane,
        "runtime_dependency_inactive",
        meson_rtx_runtime_disabled
        or (
            meson_rtx_runtime_enabled
            and api_non_gl_unavailable
            and api_else_exports_unavailable_hooks
        ),
        "RTX/vkpt runtime dependency must stay inactive until native rendering is available",
    )
    validate_lane_fact(
        rtx_lane,
        "native_family_export_inactive",
        (
            "R_RENDERER_RMLUI_FAMILY_RTX_VKPT" not in renderer_bridge_text
            and "R_RENDERER_RMLUI_FAMILY_RTX_VKPT" not in renderer_api_text
        )
        or (
            api_non_gl_unavailable
            and api_else_exports_unavailable_hooks
            and (meson_rtx_runtime_disabled or meson_rtx_runtime_enabled)
        ),
        "RTX/vkpt family export must stay inactive until native rendering is available",
    )
    validate_lane_fact(
        rtx_lane,
        "not_redirected_to_opengl",
        report.contract_facts["no_vulkan_or_rtx_to_opengl_redirect"],
        "RTX/vkpt lane must not redirect through the OpenGL RmlUi bridge",
    )
    report.lanes["rtx_vkpt"] = rtx_lane

    return report


def lane_payload(lane: RendererLaneReport) -> dict[str, Any]:
    return {
        "ok": lane.ok(),
        "label": lane.label,
        "expected_status": lane.expected_status,
        "facts": lane.facts,
        "errors": lane.errors,
    }


def json_report_payload(report: RendererMatrixReport) -> dict[str, Any]:
    lane_payloads = {
        lane_id: lane_payload(lane)
        for lane_id, lane in report.lanes.items()
    }
    return {
        "ok": report.ok(),
        "paths": {
            "repo_root": str(report.repo_root),
            "meson_build": display_path(report.meson_build_path, report.repo_root),
            "renderer_header": display_path(report.renderer_header_path, report.repo_root),
            "renderer_api_source": display_path(report.renderer_api_source_path, report.repo_root),
            "renderer_bridge_source": display_path(report.renderer_bridge_source_path, report.repo_root),
            "client_renderer_source": display_path(report.client_renderer_source_path, report.repo_root),
            "capture_checker": display_path(report.capture_checker_path, report.repo_root),
        },
        "files": report.files,
        "contract": report.contract_facts,
        "lanes": lane_payloads,
        "counts": {
            "lanes": len(report.lanes),
            "native_guarded_lanes": sum(
                1
                for lane in report.lanes.values()
                if lane.expected_status == "native_guarded"
            ),
            "blocked_lanes": sum(
                1
                for lane in report.lanes.values()
                if lane.expected_status == "blocked_until_native"
            ),
            "errors": len(report.all_errors()),
        },
        "errors": report.all_errors(),
    }


def print_json_report(report: RendererMatrixReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def print_lane_report(lane: RendererLaneReport) -> None:
    print(f"  {lane.label} ({lane.expected_status}): {'pass' if lane.ok() else 'fail'}")
    for fact_name, value in lane.facts.items():
        print(f"    {fact_name}: {yes_no(value)}")


def print_report(report: RendererMatrixReport) -> None:
    print("RmlUi renderer-family matrix guardrail:")
    print(f"  Malformed findings: {len(report.all_errors())}")
    print(f"  Meson build: {display_path(report.meson_build_path, report.repo_root)}")
    print(f"  Renderer header: {display_path(report.renderer_header_path, report.repo_root)}")
    print(f"  Renderer API: {display_path(report.renderer_api_source_path, report.repo_root)}")
    print(f"  Renderer bridge: {display_path(report.renderer_bridge_source_path, report.repo_root)}")
    print(f"  Client renderer: {display_path(report.client_renderer_source_path, report.repo_root)}")
    print(f"  Capture checker: {display_path(report.capture_checker_path, report.repo_root)}")

    print("\nContract facts:")
    for fact_name, value in report.contract_facts.items():
        print(f"  {fact_name}: {yes_no(value)}")

    print("\nRenderer lanes:")
    for lane in report.lanes.values():
        print_lane_report(lane)

    errors = report.all_errors()
    if errors:
        print("\nErrors:")
        for error in errors:
            print(f"  - {error}")
        print("\nResult: RmlUi renderer-family matrix guardrail failed.")
    else:
        print("\nResult: RmlUi renderer-family matrix guardrail passed.")


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
        help="Path to the native OpenGL RmlUi renderer bridge source.",
    )
    parser.add_argument(
        "--client-renderer-source",
        type=Path,
        default=DEFAULT_CLIENT_RENDERER_SOURCE,
        help="Path to the client renderer lifecycle source.",
    )
    parser.add_argument(
        "--capture-checker",
        type=Path,
        default=DEFAULT_CAPTURE_CHECKER,
        help="Path to the runtime capture checker.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format. Defaults to the text report.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    report = validate_renderer_matrix(
        repo_root=repo_root,
        meson_build_path=resolve_path(args.meson_build, repo_root),
        renderer_header_path=resolve_path(args.renderer_header, repo_root),
        renderer_api_source_path=resolve_path(args.renderer_api_source, repo_root),
        renderer_bridge_source_path=resolve_path(args.renderer_bridge_source, repo_root),
        client_renderer_source_path=resolve_path(args.client_renderer_source, repo_root),
        capture_checker_path=resolve_path(args.capture_checker, repo_root),
    )

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
