from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_runtime_capture as runtime_capture  # noqa: E402
import test_check_rmlui_renderer_matrix as renderer_matrix_fixture  # noqa: E402
import test_check_rmlui_vulkan_bridge_readiness as bridge_readiness_fixture  # noqa: E402


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_renderer_matrix_repo(repo_root: Path) -> None:
    bridge_readiness_fixture.write_valid_repo(repo_root)
    renderer_matrix_fixture.write_valid_repo(repo_root)
    bridge_path = repo_root / "src/renderer/rmlui_bridge.cpp"
    bridge_path.write_text(
        bridge_path.read_text(encoding="utf-8")
        + """
class R_RmlUiVulkanRenderInterface final : public Rml::RenderInterface {};
class R_RmlUiRtxVkptRenderInterface final : public Rml::RenderInterface {};
""",
        encoding="utf-8",
    )
    meson_path = repo_root / "meson.build"
    meson_path.write_text(
        """
renderer_vk_rtx_src = [
  'src/renderer/rmlui_bridge.cpp',
]
renderer_vk_src = [
  'src/renderer/rmlui_bridge.cpp',
]
"""
        + meson_path.read_text(encoding="utf-8"),
        encoding="utf-8",
    )


def fill_rect(
    rows: list[list[tuple[int, int, int]]],
    x0: int,
    y0: int,
    x1: int,
    y1: int,
    color: tuple[int, int, int],
) -> None:
    height = len(rows)
    width = len(rows[0]) if rows else 0
    for y in range(max(0, y0), min(height, y1)):
        for x in range(max(0, x0), min(width, x1)):
            rows[y][x] = color


def write_fake_tga(
    path: Path,
    *,
    width: int = 640,
    height: int = 480,
    layout: bool = True,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = bytearray(runtime_capture.TGA_HEADER_SIZE)
    header[2] = 2
    header[12:14] = width.to_bytes(2, "little")
    header[14:16] = height.to_bytes(2, "little")
    header[16] = 24
    rows = [
        [runtime_capture.LAYOUT_COLORS["body_background"] for _ in range(width)]
        for _ in range(height)
    ]

    if layout:
        panel_right = min(width - 40, max(600, 868))
        panel_inner_right = max(42, panel_right - 2)
        panel_bottom = 320 if height >= 720 else 280
        panel_inner_bottom = panel_bottom - 2
        action_top = min(height - 80, panel_bottom + 36)
        action_bottom = min(height - 20, action_top + 60)
        fill_rect(rows, 0, 0, width, 4, runtime_capture.LAYOUT_COLORS["screen_border"])
        fill_rect(rows, 0, height - 4, width, height, runtime_capture.LAYOUT_COLORS["screen_border"])
        fill_rect(rows, 0, 0, 4, height, runtime_capture.LAYOUT_COLORS["screen_border"])
        fill_rect(rows, width - 4, 0, width, height, runtime_capture.LAYOUT_COLORS["screen_border"])
        fill_rect(rows, 40, 52, 220, 62, runtime_capture.LAYOUT_COLORS["accent_text"])
        fill_rect(rows, 60, 92, 500, 102, runtime_capture.LAYOUT_COLORS["body_text"])
        fill_rect(rows, 60, 112, 420, 122, runtime_capture.LAYOUT_COLORS["body_text"])
        fill_rect(rows, 40, 140, panel_right, panel_bottom, runtime_capture.LAYOUT_COLORS["panel_border"])
        fill_rect(rows, 42, 142, panel_inner_right, panel_inner_bottom, runtime_capture.LAYOUT_COLORS["panel_background"])
        fill_rect(rows, 60, 160, 210, 170, runtime_capture.LAYOUT_COLORS["button_fill"])
        fill_rect(rows, 80, 190, 430, 200, runtime_capture.LAYOUT_COLORS["body_text"])
        fill_rect(rows, 80, 220, 500, 230, runtime_capture.LAYOUT_COLORS["body_text"])
        fill_rect(rows, 40, action_top, 160, action_bottom, runtime_capture.LAYOUT_COLORS["button_fill"])
        fill_rect(rows, 180, action_top, 300, action_bottom, runtime_capture.LAYOUT_COLORS["button_fill"])
    else:
        rows = [[(32, 32, 32) for _ in range(width)] for _ in range(height)]

    payload = bytearray()
    for row in reversed(rows):
        for red, green, blue in row:
            payload.extend((blue, green, red))

    path.write_bytes(bytes(header) + bytes(payload))


def write_valid_evidence(
    repo_root: Path,
    *,
    evidence_id: str = "rmlui_runtime_smoke_round29",
    route_id: str = "core.runtime_smoke",
    width: int = 640,
    height: int = 480,
) -> None:
    install_dir = repo_root / ".install"
    basew_dir = install_dir / "basew"
    screenshot_path = basew_dir / "screenshots" / f"{evidence_id}.tga"
    log_path = basew_dir / "logs" / f"{evidence_id}.log"
    condump_path = basew_dir / "condumps" / f"{evidence_id}.txt"

    write_text(install_dir / "worr_x86_64.exe", "fake exe")
    route_document = runtime_capture.ROUTE_DOCUMENTS[route_id]
    write_text(basew_dir / "ui/rml" / route_document, "<rml />")
    write_fake_tga(screenshot_path, width=width, height=height)
    capture_marker = (
        "RmlUi guarded capture route is active; capture after the next rendered frame."
        if route_id == "core.runtime_smoke"
        else f"RmlUi guarded menu capture route '{route_id}' is active through UIMENU_MAIN; capture after the next rendered frame."
    )
    evidence_text = f"""
{capture_marker}
RmlUi TTF font face 'WORR Display' loaded from Quake II Rerelease font fonts/RussoOne-Regular.ttf.
RmlUi TTF font engine generated text texture: strings=1 size=18 source='Quake II Rerelease: fonts/RussoOne-Regular.ttf'.
RmlUi runtime status: active=yes route='{route_id}' availability='ready' runtime='RmlUi' renderer='OpenGL RmlUi render-interface primitives' family='opengl'.
RmlUi runtime frames: updates=2 renders=2 last_realtime=123 dimensions={width}x{height}.
RmlUi runtime route counters: opens=1 closes=0 close_requests=0 synthetic_inputs=0.
RmlUi runtime input: keys=0 chars=0 mouse_moves=0 mouse_buttons=0 mouse_wheels=0 last_mouse=0,0.
Wrote {screenshot_path}
RmlUi synthetic input smoke: keys=2 chars=1 mouse_moves=1 mouse_buttons=1 mouse_wheels=1 close_requests=1 closes=1 active=no.
RmlUi runtime status: active=no route='<none>' availability='ready' runtime='RmlUi' renderer='OpenGL RmlUi render-interface primitives' family='opengl'.
RmlUi runtime frames: updates=2 renders=2 last_realtime=123 dimensions={width}x{height}.
RmlUi runtime route counters: opens=1 closes=1 close_requests=1 synthetic_inputs=1.
RmlUi runtime input: keys=2 chars=1 mouse_moves=1 mouse_buttons=1 mouse_wheels=1 last_mouse=128,192.
"""
    write_text(log_path, evidence_text)
    write_text(condump_path, evidence_text)


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *args: str,
) -> tuple[int, pytest.CaptureResult[str]]:
    result = runtime_capture.main(["--repo-root", str(repo_root), *args])
    return result, capsys.readouterr()


def test_dry_run_prints_guarded_capture_command(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(repo_root, capsys, "--dry-run")

    assert result == 0
    assert "+ui_rml_runtime_capture" in captured.out
    assert "+set r_screenshot_dir" in captured.out
    assert "+screenshottga rmlui_runtime_smoke_round29" in captured.out
    assert "+ui_rml_runtime_synthetic_input" in captured.out
    assert "+ui_rml_runtime_status" in captured.out
    assert "+quit" in captured.out


def test_dry_run_matrix_prints_guarded_viewport_commands(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(repo_root, capsys, "--dry-run", "--matrix")

    assert result == 0
    assert "+set r_geometry 960x720" in captured.out
    assert "+set r_geometry 1280x960" in captured.out
    assert "+screenshottga rmlui_runtime_smoke_round29_default_960x720" in captured.out
    assert "+screenshottga rmlui_runtime_smoke_round29_large_1280x960" in captured.out


def test_dry_run_route_matrix_prints_guarded_menu_commands(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(repo_root, capsys, "--dry-run", "--route-matrix")

    assert result == 0
    assert "+ui_rml_runtime_capture_menu main" in captured.out
    assert "+ui_rml_runtime_capture_menu game" in captured.out
    assert "+ui_rml_runtime_capture_menu download_status" in captured.out
    assert "+screenshottga rmlui_runtime_smoke_round29_main" in captured.out
    assert "+screenshottga rmlui_runtime_smoke_round29_game" in captured.out
    assert "+screenshottga rmlui_runtime_smoke_round29_download_status" in captured.out


def test_dry_run_renderer_matrix_prints_opengl_commands_and_blocked_lanes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_renderer_matrix_repo(repo_root)

    result, captured = run_checker(repo_root, capsys, "--dry-run", "--renderer-matrix")

    assert result == 0
    assert "+ui_rml_runtime_capture_menu main" in captured.out
    assert "+ui_rml_runtime_capture_menu game" in captured.out
    assert "+ui_rml_runtime_capture_menu download_status" in captured.out
    assert "OpenGL=native_guarded" in captured.out
    assert "Vulkan=blocked_until_native" in captured.out
    assert "RTX/vkpt=blocked_until_native" in captured.out
    assert "Vulkan=foundation_present_blocked" in captured.out
    assert "RTX/vkpt=foundation_present_blocked" in captured.out


def test_valid_runtime_capture_evidence_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(repo_root)

    result, captured = run_checker(
        repo_root,
        capsys,
        "--format",
        "json",
        "--write-manifest",
        ".tmp/rmlui/runtime-capture/manifest.json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["facts"]["capture_marker_seen"] is True
    assert payload["facts"]["synthetic_input_marker_seen"] is True
    assert payload["facts"]["font_geometry_marker_seen"] is True
    assert payload["facts"]["font_q2r_source_marker_seen"] is True
    assert payload["facts"]["guarded_opengl_status_seen"] is True
    assert payload["facts"]["inactive_status_seen"] is True
    assert payload["facts"]["frame_updates"] == 2
    assert payload["facts"]["frame_renders"] == 2
    assert payload["facts"]["route_opens"] == 1
    assert payload["facts"]["route_closes"] == 1
    assert payload["facts"]["route_close_requests"] == 1
    assert payload["facts"]["route_synthetic_inputs"] == 1
    assert payload["facts"]["input_keys"] == 2
    assert payload["facts"]["input_chars"] == 1
    assert payload["facts"]["input_mouse_moves"] == 1
    assert payload["facts"]["input_mouse_buttons"] == 1
    assert payload["facts"]["input_mouse_wheels"] == 1
    assert payload["facts"]["screenshot_format"] == "tga"
    assert payload["facts"]["screenshot_dimensions"] == [640, 480]
    assert payload["facts"]["screenshot_payload_nonzero"] is True
    assert payload["facts"]["layout_ok"] is True
    assert payload["facts"]["layout_assertions"]["panel_border_wraps_panel"] is True
    assert payload["facts"]["layout_assertions"]["buttons_render_below_panel"] is True
    assert payload["copied_evidence"]["screenshot"].endswith("rmlui_runtime_smoke_round29.tga")
    assert (repo_root / ".tmp/rmlui/runtime-capture/manifest.json").is_file()


def test_route_matrix_evidence_passes_for_guarded_menu_entrypoints(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    for route_id in runtime_capture.DEFAULT_ROUTE_MATRIX:
        write_valid_evidence(
            repo_root,
            evidence_id=f"rmlui_runtime_smoke_round29_{route_id}",
            route_id=route_id,
            width=960,
            height=720,
        )

    result, captured = run_checker(
        repo_root,
        capsys,
        "--route-matrix",
        "--format",
        "json",
        "--write-manifest",
        ".tmp/rmlui/runtime-capture/manifest.json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["counts"]["routes"] == 3
    assert payload["counts"]["passed"] == 3
    routes = {
        route_payload["facts"]["route_id"]: route_payload
        for route_payload in payload["routes"]
    }
    assert sorted(routes) == ["download_status", "game", "main"]
    for route_id, route_payload in routes.items():
        assert route_payload["facts"]["route_document_exists"] is True
        assert route_payload["facts"]["guarded_opengl_status_seen"] is True
        assert route_payload["facts"]["expected_dimensions"] == [960, 720]
        assert route_payload["facts"]["screenshot_dimensions"] == [960, 720]
        assert route_payload["facts"]["layout_required"] is False
        assert route_payload["facts"]["route_closes"] == 1
        assert route_payload["copied_evidence"]["screenshot"].endswith(
            f"rmlui_runtime_smoke_round29_{route_id}.tga"
        )
    assert (repo_root / ".tmp/rmlui/runtime-capture/manifest.json").is_file()


def test_renderer_matrix_evidence_combines_opengl_routes_and_static_lanes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_renderer_matrix_repo(repo_root)
    for route_id in runtime_capture.DEFAULT_ROUTE_MATRIX:
        write_valid_evidence(
            repo_root,
            evidence_id=f"rmlui_runtime_smoke_round29_{route_id}",
            route_id=route_id,
            width=960,
            height=720,
        )

    result, captured = run_checker(
        repo_root,
        capsys,
        "--renderer-matrix",
        "--format",
        "json",
        "--write-manifest",
        ".tmp/rmlui/runtime-capture/renderer-matrix.json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["counts"]["routes"] == 3
    assert payload["counts"]["route_passed"] == 3
    assert payload["counts"]["renderer_lanes"] == 3
    assert payload["counts"]["native_guarded_lanes"] == 1
    assert payload["counts"]["blocked_lanes"] == 2
    assert payload["counts"]["bridge_lanes"] == 2
    assert payload["counts"]["bridge_foundation_lanes"] == 2
    assert payload["counts"]["native_bridge_lanes"] == 0
    assert payload["counts"]["bridge_blocked_lanes"] == 2
    assert payload["counts"]["bridge_activation_complete_lanes"] == 0
    assert payload["counts"]["bridge_partial_activation_lanes"] == 2
    assert payload["counts"]["bridge_inactive_activation_lanes"] == 0
    assert payload["counts"]["bridge_activation_requirements"] == 10
    assert payload["counts"]["bridge_satisfied_activation_requirements"] == 8
    assert payload["counts"]["bridge_pending_activation_requirements"] == 2
    assert payload["counts"]["missing_bridge_requirements"] == 2
    assert payload["renderer_guardrail"]["lanes"]["opengl"]["expected_status"] == "native_guarded"
    assert payload["renderer_guardrail"]["lanes"]["vulkan"]["expected_status"] == "blocked_until_native"
    assert payload["renderer_guardrail"]["lanes"]["rtx_vkpt"]["expected_status"] == "blocked_until_native"
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["foundation_ok"] is True
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["native_bridge_claimed"] is False
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["activation_status"] == "partial_activation_blocked"
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["next_activation_requirement"] == "native_interface_export_present"
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["activation_requirements"]["runtime_dependency_enabled"] is True
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["activation_requirements"]["native_bridge_class_present"] is True
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["activation_requirements"]["native_interface_export_present"] is False
    assert payload["bridge_readiness"]["lanes"]["rtx_vkpt"]["foundation_ok"] is True
    assert payload["opengl_route_matrix"]["counts"]["passed"] == 3
    assert payload["opengl_route_matrix"]["routes"][0]["facts"]["layout_required"] is False
    assert (repo_root / ".tmp/rmlui/runtime-capture/renderer-matrix.json").is_file()


def test_renderer_matrix_fails_when_static_lane_guard_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_renderer_matrix_repo(repo_root)
    client_path = repo_root / "src/client/renderer.cpp"
    client_path.write_text(
        client_path.read_text(encoding="utf-8").replace(
            "case R_RENDERER_RMLUI_FAMILY_VULKAN:\n        return UI_RML_RENDERER_FAMILY_VULKAN;",
            "case R_RENDERER_RMLUI_FAMILY_VULKAN:\n        return UI_RML_RENDERER_FAMILY_OPENGL;",
        ),
        encoding="utf-8",
    )
    for route_id in runtime_capture.DEFAULT_ROUTE_MATRIX:
        write_valid_evidence(
            repo_root,
            evidence_id=f"rmlui_runtime_smoke_round29_{route_id}",
            route_id=route_id,
            width=960,
            height=720,
        )

    result, captured = run_checker(
        repo_root,
        capsys,
        "--renderer-matrix",
        "--format",
        "json",
    )

    payload = json.loads(captured.out)
    assert result == 1
    assert payload["ok"] is False
    assert payload["opengl_route_matrix"]["ok"] is True
    assert payload["renderer_guardrail"]["ok"] is False
    assert payload["bridge_readiness"]["ok"] is False
    assert any(
        "Vulkan/RTX RmlUi lanes must not map to OpenGL" in error["error"]
        for error in payload["errors"]
    )


def test_renderer_matrix_fails_when_bridge_readiness_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_renderer_matrix_repo(repo_root)
    vk_main_path = repo_root / "src/rend_vk/vk_main.c"
    vk_main_path.write_text(
        vk_main_path.read_text(encoding="utf-8").replace(
            "R_DrawStretchPic",
            "R_DrawStretchPicMissing",
        ),
        encoding="utf-8",
    )
    for route_id in runtime_capture.DEFAULT_ROUTE_MATRIX:
        write_valid_evidence(
            repo_root,
            evidence_id=f"rmlui_runtime_smoke_round29_{route_id}",
            route_id=route_id,
            width=960,
            height=720,
        )

    result, captured = run_checker(
        repo_root,
        capsys,
        "--renderer-matrix",
        "--format",
        "json",
    )

    payload = json.loads(captured.out)
    assert result == 1
    assert payload["ok"] is False
    assert payload["opengl_route_matrix"]["ok"] is True
    assert payload["renderer_guardrail"]["ok"] is True
    assert payload["bridge_readiness"]["ok"] is False
    assert any(
        error["source"] == "bridge_readiness"
        and "native Vulkan UI draw entrypoints must remain available" in error["error"]
        for error in payload["errors"]
    )


def test_matrix_evidence_passes_for_expected_viewports(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(
        repo_root,
        evidence_id="rmlui_runtime_smoke_round29_default_960x720",
        width=960,
        height=720,
    )
    write_valid_evidence(
        repo_root,
        evidence_id="rmlui_runtime_smoke_round29_large_1280x960",
        width=1280,
        height=960,
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        "--matrix",
        "--format",
        "json",
        "--write-manifest",
        ".tmp/rmlui/runtime-capture/manifest.json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["counts"]["viewports"] == 2
    assert payload["counts"]["passed"] == 2
    assert payload["viewports"][0]["facts"]["screenshot_dimensions"] == [960, 720]
    assert payload["viewports"][0]["facts"]["expected_dimensions"] == [960, 720]
    assert payload["viewports"][1]["facts"]["screenshot_dimensions"] == [1280, 960]
    assert payload["viewports"][1]["facts"]["expected_dimensions"] == [1280, 960]
    assert payload["viewports"][1]["facts"]["layout_ok"] is True
    assert payload["viewports"][1]["facts"]["route_closes"] == 1
    assert (repo_root / ".tmp/rmlui/runtime-capture/manifest.json").is_file()


def test_geometry_dimension_mismatch_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(repo_root, width=640, height=480)

    result, captured = run_checker(repo_root, capsys, "--geometry", "960x720")

    assert result == 1
    assert "runtime capture screenshot dimensions 640x480 did not match expected 960x720" in captured.out


def test_missing_screenshot_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(repo_root)
    (repo_root / ".install/basew/screenshots/rmlui_runtime_smoke_round29.tga").unlink()

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "runtime capture screenshot missing" in captured.out
    assert "runtime capture screenshot is not a readable TGA" in captured.out


def test_missing_font_geometry_marker_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(repo_root)
    evidence_text = """
RmlUi guarded capture route is active; capture after the next rendered frame.
RmlUi runtime status: active=yes route='core.runtime_smoke' availability='ready' runtime='RmlUi' renderer='OpenGL RmlUi render-interface primitives' family='opengl'.
RmlUi runtime frames: updates=2 renders=2 last_realtime=123 dimensions=640x480.
RmlUi runtime route counters: opens=1 closes=1 close_requests=1 synthetic_inputs=1.
RmlUi runtime input: keys=2 chars=1 mouse_moves=1 mouse_buttons=1 mouse_wheels=1 last_mouse=128,192.
RmlUi synthetic input smoke: keys=2 chars=1 mouse_moves=1 mouse_buttons=1 mouse_wheels=1 close_requests=1 closes=1 active=no.
RmlUi runtime status: active=no route='<none>' availability='ready' runtime='RmlUi' renderer='OpenGL RmlUi render-interface primitives' family='opengl'.
Wrote rmlui_runtime_smoke_round29.tga
"""
    write_text(repo_root / ".install/basew/logs/rmlui_runtime_smoke_round29.log", evidence_text)
    write_text(repo_root / ".install/basew/condumps/rmlui_runtime_smoke_round29.txt", evidence_text)

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "RmlUi TTF font texture marker was not found" in captured.out


def test_layout_assertions_fail_for_nonblank_wrong_tga(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(repo_root)
    write_fake_tga(
        repo_root / ".install/basew/screenshots/rmlui_runtime_smoke_round29.tga",
        layout=False,
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "runtime capture layout:" in captured.out
    assert "expected panel_background color count" in captured.out


def test_missing_guarded_opengl_status_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(repo_root)
    write_text(
        repo_root / ".install/basew/logs/rmlui_runtime_smoke_round29.log",
        """
RmlUi guarded capture route is active; capture after the next rendered frame.
RmlUi TTF font face 'WORR Display' loaded from Quake II Rerelease font fonts/RussoOne-Regular.ttf.
RmlUi TTF font engine generated text texture: strings=1 size=18 source='Quake II Rerelease: fonts/RussoOne-Regular.ttf'.
RmlUi runtime frames: updates=2 renders=2 last_realtime=123 dimensions=640x480.
RmlUi runtime route counters: opens=1 closes=1 close_requests=1 synthetic_inputs=1.
RmlUi runtime input: keys=2 chars=1 mouse_moves=1 mouse_buttons=1 mouse_wheels=1 last_mouse=128,192.
RmlUi synthetic input smoke: keys=2 chars=1 mouse_moves=1 mouse_buttons=1 mouse_wheels=1 close_requests=1 closes=1 active=no.
RmlUi runtime status: active=no route='<none>' availability='ready' runtime='RmlUi' renderer='OpenGL RmlUi render-interface primitives' family='opengl'.
Wrote rmlui_runtime_smoke_round29.tga
""",
    )
    write_text(repo_root / ".install/basew/condumps/rmlui_runtime_smoke_round29.txt", "")

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "guarded OpenGL active-route status was not found" in captured.out


def test_missing_synthetic_input_close_evidence_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(repo_root)
    evidence_text = """
RmlUi guarded capture route is active; capture after the next rendered frame.
RmlUi TTF font face 'WORR Display' loaded from Quake II Rerelease font fonts/RussoOne-Regular.ttf.
RmlUi TTF font engine generated text texture: strings=1 size=18 source='Quake II Rerelease: fonts/RussoOne-Regular.ttf'.
RmlUi runtime status: active=yes route='core.runtime_smoke' availability='ready' runtime='RmlUi' renderer='OpenGL RmlUi render-interface primitives' family='opengl'.
RmlUi runtime frames: updates=2 renders=2 last_realtime=123 dimensions=640x480.
RmlUi runtime route counters: opens=1 closes=0 close_requests=0 synthetic_inputs=0.
RmlUi runtime input: keys=0 chars=0 mouse_moves=0 mouse_buttons=0 mouse_wheels=0 last_mouse=0,0.
Wrote rmlui_runtime_smoke_round29.tga
"""
    write_text(repo_root / ".install/basew/logs/rmlui_runtime_smoke_round29.log", evidence_text)
    write_text(repo_root / ".install/basew/condumps/rmlui_runtime_smoke_round29.txt", evidence_text)

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "runtime synthetic input marker was not found" in captured.out
    assert "runtime inactive status after synthetic back-close was not found" in captured.out
    assert "runtime synthetic input did not record key events" in captured.out
    assert "runtime route counters did not record a route close" in captured.out
