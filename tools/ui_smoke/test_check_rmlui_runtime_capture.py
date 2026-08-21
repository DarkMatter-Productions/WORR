from __future__ import annotations

import json
import subprocess
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
    renderer: str = "opengl",
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
        else (
            f"RmlUi guarded menu capture route '{route_id}' is active through UIMENU_MAIN; capture after the next rendered frame."
            if route_id in runtime_capture.MENU_ENTRYPOINT_ROUTES
            else f"RmlUi guarded route capture '{route_id}' is active through UI_Rml_OpenRoute; capture after the next rendered frame."
        )
    )
    renderer_family = runtime_capture.RENDERER_FAMILIES[renderer]
    renderer_label = runtime_capture.RENDERER_LABELS[renderer]
    evidence_text = f"""
{capture_marker}
RmlUi TTF font face 'WORR Display' loaded from Quake II Rerelease font fonts/RussoOne-Regular.ttf.
RmlUi TTF font engine generated text texture: strings=1 size=18 source='Quake II Rerelease: fonts/RussoOne-Regular.ttf'.
RmlUi runtime status: active=yes route='{route_id}' availability='ready' runtime='RmlUi' renderer='{renderer_label} RmlUi render-interface primitives' family='{renderer_family}'.
RmlUi runtime frames: updates=2 renders=2 last_realtime=123 dimensions={width}x{height}.
RmlUi runtime route counters: opens=1 closes=0 close_requests=0 synthetic_inputs=0.
RmlUi runtime input: keys=0 chars=0 mouse_moves=0 mouse_buttons=0 mouse_wheels=0 last_mouse=0,0.
Wrote {screenshot_path}
RmlUi synthetic input smoke: keys=2 chars=1 mouse_moves=1 mouse_buttons=1 mouse_wheels=1 close_requests=1 closes=1 active=no.
RmlUi runtime status: active=no route='<none>' availability='ready' runtime='RmlUi' renderer='{renderer_label} RmlUi render-interface primitives' family='{renderer_family}'.
RmlUi runtime frames: updates=2 renders=2 last_realtime=123 dimensions={width}x{height}.
RmlUi runtime route counters: opens=1 closes=1 close_requests=1 synthetic_inputs=1.
RmlUi runtime input: keys=2 chars=1 mouse_moves=1 mouse_buttons=1 mouse_wheels=1 last_mouse=128,192.
"""
    write_text(log_path, evidence_text)
    write_text(condump_path, evidence_text)


def write_valid_navigation_evidence(
    repo_root: Path,
    renderer: str,
    *,
    evidence_id: str | None = None,
) -> Path:
    install_dir = repo_root / ".install"
    basew_dir = install_dir / "basew"
    lane_evidence_id = evidence_id or f"rmlui_runtime_smoke_round29_navigation_{renderer}"
    runtime_home = (
        repo_root
        / runtime_capture.DEFAULT_NAVIGATION_RUNTIME_DIR
        / lane_evidence_id
    )
    log_path = runtime_home / "basew" / "logs" / f"{lane_evidence_id}.log"
    write_text(install_dir / "worr_x86_64.exe", "fake exe")
    for route_id in ("main", "options", "video"):
        write_text(
            basew_dir / "ui/rml" / runtime_capture.ROUTE_DOCUMENTS[route_id],
            "<rml />",
        )

    renderer_family = runtime_capture.RENDERER_FAMILIES[renderer]
    renderer_label = runtime_capture.RENDERER_LABELS[renderer]
    lines: list[str] = []
    for name, active, route_id in runtime_capture.NAVIGATION_CHECKPOINTS:
        prefix = "[2026-08-03 12:00] "
        lines.append(prefix + runtime_capture.navigation_checkpoint_token(name))
        lines.append(
            prefix
            + "RmlUi runtime status: "
            + f"active={'yes' if active else 'no'} route='{route_id}' "
            + "availability='ready' runtime='RmlUi 6.2' "
            + f"renderer='{renderer_label} RmlUi render-interface primitives' "
            + f"family='{renderer_family}'."
        )
        lines.append(
            prefix
            + "RmlUi runtime frames: updates=2 renders=2 "
            + "last_realtime=123 dimensions=960x720."
        )
        if active:
            lines.append(
                prefix
                + "RmlUi runtime route counters: opens=1 closes=0 "
                + "close_requests=0 synthetic_inputs=0."
            )
        else:
            lines.append(
                prefix
                + "RmlUi runtime route counters: opens=1 closes=1 "
                + "close_requests=1 synthetic_inputs=0."
            )
        lines.append(
            prefix
            + "RmlUi runtime input: keys=0 chars=0 mouse_moves=0 "
            + "mouse_buttons=0 mouse_wheels=0 last_mouse=0,0."
        )
    write_text(log_path, "\n".join(lines) + "\n")
    return log_path


def write_valid_navigation_matrix_evidence(repo_root: Path) -> dict[str, Path]:
    return {
        renderer: write_valid_navigation_evidence(repo_root, renderer)
        for renderer in runtime_capture.NAVIGATION_RENDERERS
    }


def replace_navigation_checkpoint_text(
    text: str,
    checkpoint: str,
    old: str,
    new: str,
) -> str:
    marker = runtime_capture.navigation_checkpoint_token(checkpoint)
    start = text.index(marker)
    end = len(text)
    for name, _active, _route in runtime_capture.NAVIGATION_CHECKPOINTS:
        candidate = text.find(runtime_capture.navigation_checkpoint_token(name), start + len(marker))
        if candidate >= 0:
            end = min(end, candidate)
    segment = text[start:end]
    assert old in segment
    return text[:start] + segment.replace(old, new, 1) + text[end:]


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
    assert "+set r_maxfps 60" in captured.out
    assert "+set s_enable 0" in captured.out
    assert "+set win_headless 1 +set in_enable 0 +set in_grab 0" in captured.out
    assert "+set r_screenshot_message 1" in captured.out
    assert "--bootstrap-quiet-status" in captured.out
    assert "+ui_rml_reduced_motion 1" in captured.out
    assert "+ui_rml_reduced_motion 0 +quit" in captured.out
    assert f"+wait {runtime_capture.DEFAULT_CAPTURE_WAIT}" in captured.out
    assert "+screenshottga rmlui_runtime_smoke_round29" in captured.out
    assert "+ui_rml_runtime_synthetic_input" in captured.out
    assert "+ui_rml_runtime_status" in captured.out
    assert "+quit" in captured.out


def test_engine_timeout_is_reported_without_escaping(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    engine_exe = install_dir / "worr_x86_64.exe"
    write_text(engine_exe, "fake exe")
    report = runtime_capture.build_report(
        repo_root,
        install_dir,
        engine_exe,
        "basew",
        "rtx",
        repo_root / ".tmp/rmlui/runtime-capture",
        "timeout_evidence",
        "tga",
        runtime_capture.DEFAULT_ROUTE_ID,
        True,
        None,
        "640x480",
        1,
        1,
        1,
        None,
    )

    def raise_timeout(*args: object, **kwargs: object) -> None:
        raise runtime_capture.subprocess.TimeoutExpired(report.command, 2.5)

    monkeypatch.setattr(runtime_capture.subprocess, "run", raise_timeout)

    start_time = runtime_capture.run_engine_for_report(report, 2.5)

    assert start_time is not None
    assert report.ran_engine is True
    assert report.exit_code is None
    assert report.elapsed_seconds is not None
    assert report.elapsed_seconds >= 0
    assert report.errors == ["engine timed out after 2.5 seconds"]


def test_fixed_size_body_text_threshold_does_not_scale_with_viewport() -> None:
    assert runtime_capture.min_layout_count(640 * 480, "body_text") == 1024
    assert runtime_capture.min_layout_count(1920 * 1080, "body_text") == 1024


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


def test_full_route_matrix_dry_run_covers_manifest_routes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        "--dry-run",
        "--full-route-matrix",
    )

    assert result == 0
    commands = [line for line in captured.out.splitlines() if line.strip()]
    assert len(commands) == len(runtime_capture.FULL_ROUTE_MATRIX) == 58
    assert "+ui_rml_runtime_capture_menu main" in captured.out
    assert "+ui_rml_runtime_capture_route accessibility" in captured.out
    assert "+ui_rml_runtime_capture_route match_stats" in captured.out
    assert captured.out.count("+set ui_dm_initial 0") == 2
    assert captured.out.count("+set ui_dm_show_resume 1") == 2


def test_navigation_matrix_dry_run_is_headless_input_free_and_ordered(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        "--dry-run",
        "--navigation-matrix",
        "--format",
        "json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["schema"] == runtime_capture.NAVIGATION_EVIDENCE_SCHEMA
    assert payload["journey_id"] == runtime_capture.NAVIGATION_JOURNEY_ID
    assert payload["counts"]["renderer_lanes"] == 3
    assert [lane["renderer"] for lane in payload["lanes"]] == list(
        runtime_capture.NAVIGATION_RENDERERS
    )
    expected_markers = [
        runtime_capture.navigation_checkpoint_token(name)
        for name, _active, _route in runtime_capture.NAVIGATION_CHECKPOINTS
    ]
    for lane in payload["lanes"]:
        command = lane["command"]
        assert runtime_capture.command_contains_sequence(
            command, ("+set", "win_headless", "1")
        )
        assert runtime_capture.command_contains_sequence(
            command, ("+set", "in_enable", "0")
        )
        assert runtime_capture.command_contains_sequence(
            command, ("+set", "in_grab", "0")
        )
        assert runtime_capture.command_contains_sequence(
            command, ("+set", "r_fullscreen", "0")
        )
        assert runtime_capture.command_contains_sequence(
            command, ("+set", "r_renderer", lane["renderer"])
        )
        runtime_home = repo_root / lane["paths"]["runtime_home"]
        assert runtime_capture.command_set_values(command, "homedir") == [
            str(runtime_home)
        ]
        assert runtime_home.is_relative_to(repo_root / ".tmp")
        assert "+ui_rml_runtime_synthetic_input" not in command
        assert not any(token.startswith("+screenshot") for token in command)
        assert [token for token in command if token.startswith("RMLUI_NAV_CHECKPOINT:")] == expected_markers
        assert command.count("+ui_rml_runtime_back") == 3
        assert command.index("+ui_rml_runtime_capture_menu") < command.index(
            "+ui_rml_runtime_open"
        )
        options_index = command.index("options")
        video_index = command.index("video")
        first_back_index = command.index("+ui_rml_runtime_back")
        assert options_index < video_index < first_back_index


def test_navigation_command_contract_rejects_conflicting_headless_or_input_flags(
    tmp_path: Path,
) -> None:
    command = runtime_capture.build_navigation_engine_command(
        tmp_path / "worr_x86_64.exe",
        tmp_path,
        tmp_path / ".tmp/rmlui/runtime-navigation/navigation-contract",
        "basew",
        "opengl",
        "navigation-contract",
        "960x720",
        1,
        1,
    )
    command.extend(
        [
            "+set",
            "win_headless",
            "0",
            "+set",
            "in_enable",
            "1",
            "+screenshotpng",
            "forbidden-navigation-capture",
        ]
    )

    errors = runtime_capture.navigation_command_contract_errors(command)

    assert any("win_headless=1" in error for error in errors)
    assert any("in_enable=0" in error for error in errors)
    assert any("must remain log-only" in error for error in errors)


def test_navigation_report_contract_rejects_renderer_redirection(
    tmp_path: Path,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    report = runtime_capture.build_navigation_reports(
        repo_root,
        install_dir,
        install_dir / "worr_x86_64.exe",
        "basew",
        repo_root / ".tmp/rmlui/runtime-capture",
        "navigation-renderer-contract",
        "960x720",
        1,
        1,
        repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json",
    )[1]
    assert report.renderer == "vulkan"
    report.command.extend(["+set", "r_renderer", "opengl"])

    errors = runtime_capture.navigation_report_contract_errors(report)

    assert any("r_renderer=vulkan" in error for error in errors)


@pytest.mark.parametrize(
    ("option", "value"),
    (
        ("--evidence-id", "../victim"),
        ("--evidence-id", "+quit"),
        ("--evidence-id", "capture;quit"),
        ("--base-game", "../outside"),
        ("--base-game", "+quit"),
        ("--base-game", "basew/../outside"),
    ),
)
def test_navigation_matrix_rejects_unsafe_command_and_path_identifiers(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    option: str,
    value: str,
) -> None:
    repo_root = tmp_path / "repo"

    with pytest.raises(SystemExit) as exc_info:
        runtime_capture.main(
            [
                "--repo-root",
                str(repo_root),
                "--navigation-matrix",
                "--dry-run",
                option,
                value,
            ]
        )

    captured = capsys.readouterr()
    assert exc_info.value.code == 2
    assert "simple portable identifier" in captured.err
    assert not (repo_root / ".tmp").exists()


@pytest.mark.parametrize("timeout", ("nan", "inf", "1e308", "-1", "0"))
def test_navigation_matrix_rejects_nonfinite_or_nonpositive_timeout(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    timeout: str,
) -> None:
    repo_root = tmp_path / "repo"

    with pytest.raises(SystemExit) as exc_info:
        runtime_capture.main(
            [
                "--repo-root",
                str(repo_root),
                "--navigation-matrix",
                "--dry-run",
                "--timeout",
                timeout,
            ]
        )

    captured = capsys.readouterr()
    assert exc_info.value.code == 2
    assert "finite, positive" in captured.err


def test_navigation_live_runner_refuses_unverified_platform(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    reports = runtime_capture.build_navigation_reports(
        repo_root,
        install_dir,
        install_dir / "worr_x86_64.exe",
        "basew",
        repo_root / ".tmp/rmlui/runtime-capture",
        "navigation-platform",
        "960x720",
        1,
        1,
        repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json",
    )

    monkeypatch.setattr(runtime_capture.os, "name", "posix")
    monkeypatch.setattr(
        runtime_capture.subprocess,
        "run",
        lambda *_args, **_kwargs: pytest.fail("unsupported platform launched the client"),
    )

    assert runtime_capture.run_engine_for_navigation_report(reports[0], 1) is None
    assert any("verified Windows win_headless" in error for error in reports[0].errors)


def test_navigation_live_runner_uses_isolated_home_and_no_window_process_flag(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    engine_exe = install_dir / "worr_x86_64.exe"
    write_text(engine_exe, "fake exe")
    report = runtime_capture.build_navigation_reports(
        repo_root,
        install_dir,
        engine_exe,
        "basew",
        repo_root / ".tmp/rmlui/runtime-capture",
        "navigation-launch",
        "960x720",
        1,
        1,
        repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json",
    )[0]
    captured_kwargs: dict[str, object] = {}

    def fake_run(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        captured_kwargs.update(kwargs)
        return subprocess.CompletedProcess(command, 0)

    monkeypatch.setattr(runtime_capture.os, "name", "nt")
    monkeypatch.setattr(runtime_capture.subprocess, "run", fake_run)

    assert runtime_capture.run_engine_for_navigation_report(report, 1) is not None
    assert report.runtime_home.is_dir()
    assert report.runtime_home.is_relative_to(repo_root / ".tmp")
    assert captured_kwargs["stdin"] is subprocess.DEVNULL
    assert captured_kwargs["creationflags"] == getattr(
        subprocess, "CREATE_NO_WINDOW", 0
    )


def test_navigation_live_runner_rejects_redirected_scratch_root(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    engine_exe = install_dir / "worr_x86_64.exe"
    write_text(engine_exe, "fake exe")
    outside = tmp_path / "outside-runtime-home"
    outside.mkdir(parents=True)
    runtime_root = repo_root / runtime_capture.DEFAULT_NAVIGATION_RUNTIME_DIR
    runtime_root.parent.mkdir(parents=True)
    try:
        runtime_root.symlink_to(outside, target_is_directory=True)
    except (NotImplementedError, OSError) as exc:
        pytest.skip(f"directory symlinks are unavailable on this host: {exc}")
    report = runtime_capture.build_navigation_reports(
        repo_root,
        install_dir,
        engine_exe,
        "basew",
        repo_root / ".tmp/rmlui/runtime-capture",
        "navigation-redirect",
        "960x720",
        1,
        1,
        repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json",
    )[0]

    monkeypatch.setattr(runtime_capture.os, "name", "nt")
    monkeypatch.setattr(
        runtime_capture.subprocess,
        "run",
        lambda *_args, **_kwargs: pytest.fail("redirected scratch root launched the client"),
    )

    assert runtime_capture.run_engine_for_navigation_report(report, 1) is None
    assert any(
        "link or reparse point" in error or "resolves outside" in error
        for error in report.errors
    )


def test_navigation_live_runner_rechecks_mocked_reparse_after_leaf_claim(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    engine_exe = install_dir / "worr_x86_64.exe"
    write_text(engine_exe, "fake exe")
    report = runtime_capture.build_navigation_reports(
        repo_root,
        install_dir,
        engine_exe,
        "basew",
        repo_root / ".tmp/rmlui/runtime-capture",
        "navigation-mocked-reparse",
        "960x720",
        1,
        1,
        repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json",
    )[0]
    redirected_component = report.runtime_home.absolute()
    original_probe = runtime_capture.path_is_link_or_reparse
    redirected_component_probes = 0

    def mocked_probe(path: Path) -> bool:
        nonlocal redirected_component_probes
        if path.absolute() == redirected_component:
            redirected_component_probes += 1
            return redirected_component_probes >= 3
        return original_probe(path)

    monkeypatch.setattr(runtime_capture.os, "name", "nt")
    monkeypatch.setattr(runtime_capture, "path_is_link_or_reparse", mocked_probe)
    monkeypatch.setattr(
        runtime_capture.subprocess,
        "run",
        lambda *_args, **_kwargs: pytest.fail("reparse component launched the client"),
    )

    assert runtime_capture.run_engine_for_navigation_report(report, 1) is None
    assert redirected_component_probes == 3
    assert report.runtime_home.is_dir()
    assert any("link or reparse point" in error for error in report.errors)


def test_navigation_live_runner_atomically_refuses_preexisting_empty_lane_home(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    engine_exe = install_dir / "worr_x86_64.exe"
    write_text(engine_exe, "fake exe")
    report = runtime_capture.build_navigation_reports(
        repo_root,
        install_dir,
        engine_exe,
        "basew",
        repo_root / ".tmp/rmlui/runtime-capture",
        "navigation-existing-empty-home",
        "960x720",
        1,
        1,
        repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json",
    )[0]
    report.runtime_home.mkdir(parents=True)
    assert not any(report.runtime_home.iterdir())

    monkeypatch.setattr(runtime_capture.os, "name", "nt")
    monkeypatch.setattr(
        runtime_capture.subprocess,
        "run",
        lambda *_args, **_kwargs: pytest.fail("pre-existing lane home launched the client"),
    )

    assert runtime_capture.run_engine_for_navigation_report(report, 1) is None
    assert any("atomic lane claim refused" in error for error in report.errors)


def test_navigation_live_runner_refuses_nonempty_lane_home(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    engine_exe = install_dir / "worr_x86_64.exe"
    write_text(engine_exe, "fake exe")
    report = runtime_capture.build_navigation_reports(
        repo_root,
        install_dir,
        engine_exe,
        "basew",
        repo_root / ".tmp/rmlui/runtime-capture",
        "navigation-stale-home",
        "960x720",
        1,
        1,
        repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json",
    )[0]
    stale_config = report.runtime_home / "basew/config.cfg"
    stale_log = report.log_path
    write_text(stale_config, "set in_enable 1\nset r_fullscreen 1\n")
    write_text(stale_log, "stale checkpoint\n")

    monkeypatch.setattr(runtime_capture.os, "name", "nt")
    monkeypatch.setattr(
        runtime_capture.subprocess,
        "run",
        lambda *_args, **_kwargs: pytest.fail("stale runtime home launched the client"),
    )

    assert runtime_capture.run_engine_for_navigation_report(report, 1) is None
    assert any("atomic lane claim refused" in error for error in report.errors)
    assert stale_config.is_file()
    assert stale_log.is_file()


def test_navigation_contract_reports_resolve_failures(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".install"
    report = runtime_capture.build_navigation_reports(
        repo_root,
        install_dir,
        install_dir / "worr_x86_64.exe",
        "basew",
        repo_root / ".tmp/rmlui/runtime-capture",
        "navigation-resolve-failure",
        "960x720",
        1,
        1,
        repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json",
    )[0]
    runtime_root = (
        repo_root / runtime_capture.DEFAULT_NAVIGATION_RUNTIME_DIR
    ).absolute()
    original_resolve = Path.resolve

    def mocked_resolve(path: Path, *args: object, **kwargs: object) -> Path:
        if path.absolute() == runtime_root:
            raise RuntimeError("mocked reparse loop")
        return original_resolve(path, *args, **kwargs)

    monkeypatch.setattr(Path, "resolve", mocked_resolve)

    errors = runtime_capture.navigation_report_contract_errors(report)

    assert any(
        "could not resolve navigation runtime scratch root" in error
        and "mocked reparse loop" in error
        for error in errors
    )


def test_dry_run_save_route_uses_general_route_capture(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        "--dry-run",
        "--route-id",
        "loadgame",
    )

    assert result == 0
    assert "+ui_rml_runtime_capture_route loadgame" in captured.out
    assert "+screenshottga rmlui_runtime_smoke_round29" in captured.out


def test_capture_registry_covers_every_manifest_route() -> None:
    manifest = json.loads(
        (SCRIPT_DIR / "rmlui_manifest.json").read_text(encoding="utf-8")
    )
    manifest_routes = {route["id"] for route in manifest["routes"]}
    assert set(runtime_capture.ROUTE_DOCUMENTS) == manifest_routes | {
        runtime_capture.DEFAULT_ROUTE_ID
    }


def test_dry_run_server_route_forces_local_source(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        "--dry-run",
        "--route-id",
        "servers",
        "--server-address",
        "127.0.0.1:27919",
    )

    assert result == 0
    assert runtime_capture.ROUTE_DOCUMENTS["servers"] == Path("utility/servers.rml")
    assert "+ui_rml_reduced_motion 1 +set adr0 127.0.0.1:27919 +set ui_server_source local" in captured.out
    assert "+ui_rml_runtime_capture_route servers" in captured.out


def test_dry_run_ui_list_route_seeds_published_cvars(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        "--dry-run",
        "--route-id",
        "ui_list",
        "--seed-cvar",
        "ui_list_title=Callvote: Map",
        "--seed-cvar",
        "ui_list_item_show_0=1",
    )

    assert result == 0
    assert runtime_capture.ROUTE_DOCUMENTS["ui_list"] == Path("utility/ui_list.rml")
    assert "+set ui_list_title Callvote: Map +set ui_list_item_show_0 1" in captured.out
    assert "+ui_rml_runtime_capture_route ui_list" in captured.out


@pytest.mark.parametrize(
    "seed",
    ("missing_separator", "bad-name=value", "9bad=value"),
)
def test_invalid_seed_cvar_is_rejected(
    tmp_path: Path,
    seed: str,
) -> None:
    with pytest.raises(SystemExit):
        runtime_capture.main(
            [
                "--repo-root",
                str(tmp_path / "repo"),
                "--dry-run",
                "--seed-cvar",
                seed,
            ]
        )


def test_dry_run_renderer_matrix_prints_native_renderer_lanes(
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
    assert "Vulkan=native_guarded" in captured.out
    assert "RTX/vkpt=native_guarded" in captured.out
    assert "Vulkan=native_guarded" in captured.out
    assert "RTX/vkpt=native_guarded" in captured.out


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


def test_valid_vulkan_runtime_capture_evidence_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_evidence(repo_root, renderer="vulkan")

    result, captured = run_checker(
        repo_root,
        capsys,
        "--renderer",
        "vulkan",
        "--format",
        "json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["facts"]["renderer"] == "vulkan"
    assert payload["facts"]["guarded_renderer_status_seen"] is True
    assert payload["facts"]["guarded_opengl_status_seen"] is False
    assert "+set r_renderer vulkan" in " ".join(payload["command"])


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


def test_navigation_matrix_evidence_passes_ordered_renderer_journeys(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_navigation_matrix_evidence(repo_root)
    manifest_path = repo_root / ".tmp/rmlui/runtime-capture/navigation-matrix.json"

    result, captured = run_checker(
        repo_root,
        capsys,
        "--navigation-matrix",
        "--format",
        "json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["counts"] == {
        "checkpoints_per_lane": 6,
        "errors": 0,
        "failed": 0,
        "passed": 3,
        "renderer_lanes": 3,
    }
    expected_order = [
        name for name, _active, _route in runtime_capture.NAVIGATION_CHECKPOINTS
    ]
    expected_routes = [
        route for _name, _active, route in runtime_capture.NAVIGATION_CHECKPOINTS
    ]
    for lane in payload["lanes"]:
        assert lane["facts"]["checkpoint_order_ok"] is True
        assert lane["facts"]["observed_checkpoint_order"] == expected_order
        assert lane["facts"]["synthetic_input_command_absent"] is True
        assert lane["facts"]["synthetic_input_marker_absent"] is True
        assert lane["facts"]["final_inactive"] is True
        assert [checkpoint["route"] for checkpoint in lane["checkpoints"]] == expected_routes
        assert all(
            checkpoint["renderer_family"] == lane["renderer_family"]
            for checkpoint in lane["checkpoints"]
        )
        assert all(
            checkpoint["frame_renders"] > 0
            for checkpoint in lane["checkpoints"]
            if checkpoint["expected_active"]
        )
        assert all(
            checkpoint["input_keys"] == 0
            and checkpoint["input_chars"] == 0
            and checkpoint["input_mouse_moves"] == 0
            and checkpoint["input_mouse_buttons"] == 0
            and checkpoint["input_mouse_wheels"] == 0
            for checkpoint in lane["checkpoints"]
        )
        copied_log = repo_root / lane["copied_evidence"]["log"]
        assert copied_log.is_file()
    assert manifest_path.is_file()
    assert json.loads(manifest_path.read_text(encoding="utf-8")) == payload


@pytest.mark.parametrize(
    ("failure", "expected_error"),
    (
        ("wrong_back_route", "checkpoint 'back_options' route was 'main'"),
        ("missing_checkpoint", "checkpoints were missing, duplicated, or out of order"),
        ("wrong_family", "checkpoint 'video' renderer family was 'vulkan'"),
        ("zero_render", "checkpoint 'video' did not record an update and render"),
        ("final_active", "did not finish inactive on route '<none>'"),
        ("nonzero_input", "checkpoint 'options' input_keys was 1"),
        ("synthetic_marker", "log contains a synthetic input marker"),
    ),
)
def test_navigation_matrix_rejects_invalid_checkpoint_evidence(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    failure: str,
    expected_error: str,
) -> None:
    repo_root = tmp_path / "repo"
    paths = write_valid_navigation_matrix_evidence(repo_root)
    log_path = paths["opengl"]
    text = log_path.read_text(encoding="utf-8")
    if failure == "wrong_back_route":
        text = replace_navigation_checkpoint_text(
            text, "back_options", "route='options'", "route='main'"
        )
    elif failure == "missing_checkpoint":
        text = text.replace(
            runtime_capture.navigation_checkpoint_token("back_options"),
            "RMLUI_NAV_CHECKPOINT_MISSING:back_options",
            1,
        )
    elif failure == "wrong_family":
        text = replace_navigation_checkpoint_text(
            text, "video", "family='opengl'", "family='vulkan'"
        )
    elif failure == "zero_render":
        text = replace_navigation_checkpoint_text(
            text, "video", "updates=2 renders=2", "updates=2 renders=0"
        )
    elif failure == "final_active":
        text = replace_navigation_checkpoint_text(
            text,
            "inactive",
            "active=no route='<none>'",
            "active=yes route='main'",
        )
    elif failure == "nonzero_input":
        text = replace_navigation_checkpoint_text(
            text, "options", "keys=0", "keys=1"
        )
    elif failure == "synthetic_marker":
        text += runtime_capture.SYNTHETIC_INPUT_MARKER + " forbidden\n"
    else:
        raise AssertionError(f"unhandled fixture mutation: {failure}")
    write_text(log_path, text)

    result, captured = run_checker(
        repo_root,
        capsys,
        "--navigation-matrix",
        "--format",
        "json",
    )

    payload = json.loads(captured.out)
    assert result == 1
    assert payload["ok"] is False
    assert any(expected_error in error["error"] for error in payload["errors"])


def test_navigation_matrix_rejects_synthetic_input_command(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_navigation_matrix_evidence(repo_root)
    original_builder = runtime_capture.build_navigation_engine_command

    def build_forbidden_command(*args: object, **kwargs: object) -> list[str]:
        command = original_builder(*args, **kwargs)  # type: ignore[arg-type]
        command.insert(-3, "+ui_rml_runtime_synthetic_input")
        return command

    monkeypatch.setattr(
        runtime_capture,
        "build_navigation_engine_command",
        build_forbidden_command,
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        "--navigation-matrix",
        "--format",
        "json",
    )

    payload = json.loads(captured.out)
    assert result == 1
    assert payload["ok"] is False
    assert all(
        any("must not invoke runtime synthetic input" in error for error in lane["errors"])
        for lane in payload["lanes"]
    )


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
    assert payload["counts"]["native_guarded_lanes"] == 3
    assert payload["counts"]["blocked_lanes"] == 0
    assert payload["counts"]["bridge_lanes"] == 2
    assert payload["counts"]["bridge_foundation_lanes"] == 2
    assert payload["counts"]["native_bridge_lanes"] == 2
    assert payload["counts"]["bridge_blocked_lanes"] == 0
    assert payload["counts"]["bridge_activation_complete_lanes"] == 2
    assert payload["counts"]["bridge_partial_activation_lanes"] == 0
    assert payload["counts"]["bridge_inactive_activation_lanes"] == 0
    assert payload["counts"]["bridge_activation_requirements"] == 10
    assert payload["counts"]["bridge_satisfied_activation_requirements"] == 10
    assert payload["counts"]["bridge_pending_activation_requirements"] == 0
    assert payload["counts"]["missing_bridge_requirements"] == 0
    assert payload["renderer_guardrail"]["lanes"]["opengl"]["expected_status"] == "native_guarded"
    assert payload["renderer_guardrail"]["lanes"]["vulkan"]["expected_status"] == "native_guarded"
    assert payload["renderer_guardrail"]["lanes"]["rtx_vkpt"]["expected_status"] == "native_guarded"
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["foundation_ok"] is True
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["native_bridge_claimed"] is True
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["activation_status"] == "activation_complete"
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["next_activation_requirement"] is None
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["activation_requirements"]["runtime_dependency_enabled"] is True
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["activation_requirements"]["native_bridge_class_present"] is True
    assert payload["bridge_readiness"]["lanes"]["vulkan"]["activation_requirements"]["native_interface_export_present"] is True
    assert payload["bridge_readiness"]["lanes"]["rtx_vkpt"]["foundation_ok"] is True
    assert payload["bridge_readiness"]["lanes"]["rtx_vkpt"]["native_bridge_claimed"] is True
    assert payload["bridge_readiness"]["lanes"]["rtx_vkpt"]["activation_status"] == "activation_complete"
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


def test_custom_install_defaults_to_its_staged_engine(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    custom_install = Path(".tmp/rmlui/custom-install")

    result = runtime_capture.main(
        [
            "--repo-root",
            str(repo_root),
            "--install-dir",
            custom_install.as_posix(),
            "--dry-run",
        ]
    )

    assert result == 0
    command = capsys.readouterr().out
    assert str((repo_root / custom_install / "worr_x86_64.exe").resolve()) in command
