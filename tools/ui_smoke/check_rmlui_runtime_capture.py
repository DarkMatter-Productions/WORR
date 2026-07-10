"""Run or validate the guarded RmlUi runtime screenshot smoke evidence."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import check_rmlui_renderer_matrix as renderer_matrix
import check_rmlui_vulkan_bridge_readiness as bridge_readiness


DEFAULT_INSTALL_DIR = Path(".install")
DEFAULT_ENGINE_EXE = DEFAULT_INSTALL_DIR / "worr_x86_64.exe"
DEFAULT_BASE_GAME = "basew"
DEFAULT_EVIDENCE_DIR = Path(".tmp/rmlui/runtime-capture")
DEFAULT_EVIDENCE_ID = "rmlui_runtime_smoke_round29"
DEFAULT_STARTUP_WAIT = 120
DEFAULT_CAPTURE_WAIT = 12
DEFAULT_STATUS_WAIT = 12
DEFAULT_SCREENSHOT_FORMAT = "tga"
DEFAULT_VIEWPORT_MATRIX = (
    ("default_960x720", "960x720"),
    ("large_1280x960", "1280x960"),
)
DEFAULT_ROUTE_MATRIX = ("main", "game", "download_status")
DEFAULT_ROUTE_MATRIX_GEOMETRY = "960x720"
DEFAULT_ROUTE_ID = "core.runtime_smoke"

ROUTE_DOCUMENTS = {
    "core.runtime_smoke": Path("core/runtime_smoke.rml"),
    "main": Path("shell/main.rml"),
    "game": Path("shell/game.rml"),
    "download_status": Path("shell/download_status.rml"),
}

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
TGA_HEADER_SIZE = 18

STATUS_MARKER = "RmlUi runtime status:"
FRAMES_MARKER = "RmlUi runtime frames:"
ROUTE_COUNTERS_MARKER = "RmlUi runtime route counters:"
INPUT_MARKER = "RmlUi runtime input:"
CAPTURE_MARKER = "RmlUi guarded capture route is active"
MENU_CAPTURE_MARKER = "RmlUi guarded menu capture route"
SYNTHETIC_INPUT_MARKER = "RmlUi synthetic input smoke:"
FONT_GEOMETRY_MARKER = "RmlUi TTF font engine generated text texture"
FONT_Q2R_SOURCE_MARKER = "loaded from Quake II Rerelease font"
SCREENSHOT_MARKER = "Wrote "

LAYOUT_COLORS = {
    "body_background": (18, 17, 15),
    "screen_border": (63, 55, 42),
    "panel_background": (39, 35, 29),
    "panel_border": (185, 155, 91),
    "button_fill": (229, 182, 79),
    "body_text": (243, 238, 228),
    "accent_text": (255, 217, 103),
}

LAYOUT_MIN_COUNT_RULES = {
    "body_background": (0.05, 2000),
    "screen_border": (0.005, 512),
    "panel_background": (0.05, 2000),
    "panel_border": (0.002, 256),
    "button_fill": (0.01, 1000),
    "body_text": (0.005, 512),
    "accent_text": (0.001, 128),
}


@dataclass
class TgaImage:
    width: int
    height: int
    rows: list[list[tuple[int, int, int]]]


@dataclass
class RuntimeCaptureReport:
    repo_root: Path
    install_dir: Path
    engine_exe: Path
    base_game: str
    evidence_dir: Path
    evidence_id: str
    screenshot_format: str
    route_id: str
    require_layout: bool
    viewport_name: str | None
    geometry: str | None
    expected_dimensions: tuple[int, int] | None
    command: list[str]
    screenshot_path: Path
    log_path: Path
    condump_path: Path
    manifest_path: Path | None
    ran_engine: bool = False
    exit_code: int | None = None
    elapsed_seconds: float | None = None
    copied_evidence: dict[str, str] = field(default_factory=dict)
    facts: dict[str, Any] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.errors


def resolve_path(path: Path, repo_root: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root.resolve()))
    except ValueError:
        return str(path)


def read_text_if_exists(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def read_png_dimensions(path: Path) -> tuple[int, int] | None:
    if not path.is_file():
        return None

    with path.open("rb") as f:
        header = f.read(24)

    if len(header) < 24 or not header.startswith(PNG_SIGNATURE):
        return None

    width = int.from_bytes(header[16:20], "big")
    height = int.from_bytes(header[20:24], "big")
    if width <= 0 or height <= 0:
        return None

    return width, height


def read_tga_dimensions(path: Path) -> tuple[int, int] | None:
    if not path.is_file():
        return None

    with path.open("rb") as f:
        header = f.read(TGA_HEADER_SIZE)

    if len(header) < TGA_HEADER_SIZE:
        return None

    image_type = header[2]
    width = int.from_bytes(header[12:14], "little")
    height = int.from_bytes(header[14:16], "little")
    pixel_depth = header[16]
    if image_type != 2 or width <= 0 or height <= 0 or pixel_depth not in (24, 32):
        return None

    return width, height


def read_tga_image(path: Path) -> TgaImage | None:
    if not path.is_file():
        return None

    data = path.read_bytes()
    if len(data) < TGA_HEADER_SIZE:
        return None

    header = data[:TGA_HEADER_SIZE]
    id_length = header[0]
    color_map_type = header[1]
    image_type = header[2]
    width = int.from_bytes(header[12:14], "little")
    height = int.from_bytes(header[14:16], "little")
    pixel_depth = header[16]
    descriptor = header[17]

    if (
        color_map_type != 0
        or image_type != 2
        or width <= 0
        or height <= 0
        or pixel_depth not in (24, 32)
    ):
        return None

    bytes_per_pixel = pixel_depth // 8
    pixel_offset = TGA_HEADER_SIZE + id_length
    expected_size = pixel_offset + width * height * bytes_per_pixel
    if len(data) < expected_size:
        return None

    top_origin = bool(descriptor & 0x20)
    right_origin = bool(descriptor & 0x10)
    rows = [[(0, 0, 0) for _ in range(width)] for _ in range(height)]

    for file_y in range(height):
        y = file_y if top_origin else height - 1 - file_y
        for file_x in range(width):
            x = width - 1 - file_x if right_origin else file_x
            offset = pixel_offset + (file_y * width + file_x) * bytes_per_pixel
            blue, green, red = data[offset : offset + 3]
            rows[y][x] = (red, green, blue)

    return TgaImage(width=width, height=height, rows=rows)


def read_screenshot_dimensions(path: Path, screenshot_format: str) -> tuple[int, int] | None:
    if screenshot_format == "png":
        return read_png_dimensions(path)
    if screenshot_format == "tga":
        return read_tga_dimensions(path)
    raise ValueError(f"unsupported screenshot format: {screenshot_format}")


def screenshot_payload_is_nonzero(path: Path, screenshot_format: str) -> bool | None:
    if not path.is_file():
        return None
    if screenshot_format != "tga":
        return None

    with path.open("rb") as f:
        header = f.read(TGA_HEADER_SIZE)
        if len(header) < TGA_HEADER_SIZE:
            return False
        f.seek(TGA_HEADER_SIZE + header[0])
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                return False
            if any(chunk):
                return True


def empty_bbox() -> dict[str, int | None]:
    return {"min_x": None, "min_y": None, "max_x": None, "max_y": None}


def update_bbox(bbox: dict[str, int | None], x: int, y: int) -> None:
    if bbox["min_x"] is None or x < bbox["min_x"]:
        bbox["min_x"] = x
    if bbox["min_y"] is None or y < bbox["min_y"]:
        bbox["min_y"] = y
    if bbox["max_x"] is None or x > bbox["max_x"]:
        bbox["max_x"] = x
    if bbox["max_y"] is None or y > bbox["max_y"]:
        bbox["max_y"] = y


def bbox_as_list(bbox: dict[str, int | None]) -> list[int] | None:
    if any(value is None for value in bbox.values()):
        return None
    return [
        int(bbox["min_x"]),
        int(bbox["min_y"]),
        int(bbox["max_x"]),
        int(bbox["max_y"]),
    ]


def bbox_width(bbox: dict[str, int | None]) -> int:
    values = bbox_as_list(bbox)
    if values is None:
        return 0
    return values[2] - values[0] + 1


def bbox_height(bbox: dict[str, int | None]) -> int:
    values = bbox_as_list(bbox)
    if values is None:
        return 0
    return values[3] - values[1] + 1


def count_color_below(image: TgaImage, color: tuple[int, int, int], y_min: int) -> int:
    count = 0
    for y, row in enumerate(image.rows):
        if y <= y_min:
            continue
        count += sum(1 for pixel in row if pixel == color)
    return count


def min_layout_count(total_pixels: int, color_name: str) -> int:
    ratio, floor = LAYOUT_MIN_COUNT_RULES[color_name]
    return max(floor, int(total_pixels * ratio))


def analyze_tga_layout(path: Path) -> tuple[dict[str, Any], list[str]]:
    image = read_tga_image(path)
    if image is None:
        return {
            "layout_checked": False,
            "layout_ok": False,
            "layout_color_counts": {},
            "layout_bounding_boxes": {},
            "layout_assertions": {},
        }, ["screenshot is not a parseable uncompressed true-color TGA"]

    total_pixels = image.width * image.height
    color_by_value = {value: name for name, value in LAYOUT_COLORS.items()}
    regions: dict[str, dict[str, Any]] = {
        name: {"count": 0, "bbox": empty_bbox()}
        for name in LAYOUT_COLORS
    }

    for y, row in enumerate(image.rows):
        for x, pixel in enumerate(row):
            name = color_by_value.get(pixel)
            if not name:
                continue
            regions[name]["count"] += 1
            update_bbox(regions[name]["bbox"], x, y)

    color_counts = {
        name: int(region["count"])
        for name, region in regions.items()
    }
    bounding_boxes = {
        name: bbox_as_list(region["bbox"])
        for name, region in regions.items()
    }

    errors: list[str] = []
    assertions: dict[str, bool] = {}
    for color_name in LAYOUT_COLORS:
        count = color_counts[color_name]
        minimum = min_layout_count(total_pixels, color_name)
        assertions[f"{color_name}_present"] = count >= minimum
        if count < minimum:
            errors.append(
                f"expected {color_name} color count >= {minimum}, found {count}"
            )

    panel_bbox = regions["panel_background"]["bbox"]
    panel_border_bbox = regions["panel_border"]["bbox"]
    button_bbox = regions["button_fill"]["bbox"]
    body_text_bbox = regions["body_text"]["bbox"]
    accent_text_bbox = regions["accent_text"]["bbox"]

    panel_wide = (
        bbox_width(panel_bbox) >= max(80, int(image.width * 0.45))
        and bbox_height(panel_bbox) >= max(50, int(image.height * 0.15))
    )
    assertions["panel_background_has_layout_extent"] = panel_wide
    if not panel_wide:
        errors.append("panel background does not have the expected route layout extent")

    panel_values = bbox_as_list(panel_bbox)
    border_values = bbox_as_list(panel_border_bbox)
    if panel_values is not None and border_values is not None:
        border_wraps_panel = (
            border_values[0] <= panel_values[0]
            and border_values[1] <= panel_values[1]
            and border_values[2] >= panel_values[2]
            and border_values[3] >= panel_values[3]
        )
    else:
        border_wraps_panel = False
    assertions["panel_border_wraps_panel"] = border_wraps_panel
    if not border_wraps_panel:
        errors.append("panel border does not wrap the panel background")

    button_below_panel_count = 0
    if panel_values is not None:
        button_below_panel_count = count_color_below(
            image,
            LAYOUT_COLORS["button_fill"],
            panel_values[3] + 8,
        )
    button_below_panel = button_below_panel_count >= max(500, int(total_pixels * 0.005))
    assertions["buttons_render_below_panel"] = button_below_panel
    if not button_below_panel:
        errors.append("button fill color was not found below the contract panel")

    body_text_values = bbox_as_list(body_text_bbox)
    if panel_values is not None and body_text_values is not None:
        body_text_reaches_content = (
            body_text_values[1] < panel_values[1]
            and body_text_values[3] <= panel_values[3]
        )
    else:
        body_text_reaches_content = False
    assertions["body_text_spans_summary_and_panel"] = body_text_reaches_content
    if not body_text_reaches_content:
        errors.append("body text geometry does not span the summary and panel regions")

    accent_text_values = bbox_as_list(accent_text_bbox)
    button_values = bbox_as_list(button_bbox)
    if accent_text_values is not None and button_values is not None:
        accent_above_buttons = accent_text_values[1] < button_values[1]
    else:
        accent_above_buttons = False
    assertions["accent_text_above_buttons"] = accent_above_buttons
    if not accent_above_buttons:
        errors.append("accent/title text is not positioned above the action buttons")

    facts = {
        "layout_checked": True,
        "layout_ok": not errors,
        "layout_color_counts": color_counts,
        "layout_bounding_boxes": bounding_boxes,
        "layout_assertions": assertions,
        "layout_button_fill_below_panel_count": button_below_panel_count,
    }
    return facts, errors


def analyze_screenshot_layout(path: Path, screenshot_format: str) -> tuple[dict[str, Any], list[str]]:
    if screenshot_format != "tga":
        return {
            "layout_checked": False,
            "layout_ok": False,
            "layout_color_counts": {},
            "layout_bounding_boxes": {},
            "layout_assertions": {},
        }, ["layout assertions require TGA evidence"]

    return analyze_tga_layout(path)


def screenshot_extension(screenshot_format: str) -> str:
    return f".{screenshot_format}"


def screenshot_command(screenshot_format: str) -> str:
    return f"+screenshot{screenshot_format}"


def parse_geometry_dimensions(geometry: str | None) -> tuple[int, int] | None:
    if not geometry:
        return None

    parts = geometry.lower().split("x", 1)
    if len(parts) != 2:
        raise ValueError(f"invalid geometry '{geometry}', expected WIDTHxHEIGHT")

    width = int(parts[0])
    height = int(parts[1])
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid geometry '{geometry}', dimensions must be positive")

    return width, height


def parse_counter_line(text: str, marker: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for line in text.splitlines():
        if marker not in line:
            continue
        for token in line.replace(",", " ").replace(".", " ").split():
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            try:
                values[key] = int(value)
            except ValueError:
                continue
    return values


def runtime_status_is_guarded_opengl(text: str, route_id: str) -> bool:
    for line in text.splitlines():
        if STATUS_MARKER not in line:
            continue
        if (
            "active=yes" in line
            and f"route='{route_id}'" in line
            and "family='opengl'" in line
        ):
            return True
    return False


def runtime_status_inactive_seen(text: str) -> bool:
    for line in text.splitlines():
        if STATUS_MARKER in line and "active=no" in line:
            return True
    return False


def build_engine_command(
    engine_exe: Path,
    install_dir: Path,
    base_game: str,
    evidence_id: str,
    screenshot_format: str,
    route_id: str,
    geometry: str | None,
    startup_wait: int,
    capture_wait: int,
    status_wait: int,
) -> list[str]:
    command = [
        str(engine_exe),
        "+set",
        "basedir",
        str(install_dir),
        "+set",
        "game",
        base_game,
        "+set",
        "r_renderer",
        "opengl",
        "+set",
        "r_fullscreen",
        "0",
    ]

    if geometry:
        command.extend(
            [
                "+set",
                "r_geometry",
                geometry,
            ]
        )

    command.extend(
        [
            "+set",
            "ui_rml_enable",
            "1",
            "+set",
            "r_screenshot_async",
            "0",
            "+set",
            "r_screenshot_dir",
            str(install_dir / base_game / "screenshots"),
            "+set",
            "logfile",
            "1",
            "+set",
            "logfile_flush",
            "1",
            "+set",
            "logfile_name",
            evidence_id,
            "+wait",
            str(startup_wait),
        ]
    )
    if route_id == DEFAULT_ROUTE_ID:
        command.append("+ui_rml_runtime_capture")
    else:
        command.extend(
            [
                "+ui_rml_runtime_capture_menu",
                route_id,
            ]
        )

    command.extend(
        [
            "+wait",
            str(capture_wait),
            screenshot_command(screenshot_format),
            evidence_id,
            "+wait",
            str(status_wait),
            "+ui_rml_runtime_synthetic_input",
            "+wait",
            str(status_wait),
            "+ui_rml_runtime_status",
            "+quit",
        ]
    )
    return command


def build_run_environment(engine_exe: Path) -> dict[str, str]:
    env = os.environ.copy()
    path_entries = [str(engine_exe.parent)]

    rmlui_dll = next(engine_exe.parent.rglob("rmlui_core.dll"), None)
    if rmlui_dll:
        path_entries.append(str(rmlui_dll.parent))

    env["PATH"] = os.pathsep.join(path_entries + [env.get("PATH", "")])
    return env


def collect_evidence(report: RuntimeCaptureReport) -> None:
    report.evidence_dir.mkdir(parents=True, exist_ok=True)

    candidates = {
        "screenshot": report.screenshot_path,
        "log": report.log_path,
        "condump": report.condump_path,
    }
    for name, source in candidates.items():
        if not source.is_file():
            continue
        target = report.evidence_dir / source.name
        shutil.copy2(source, target)
        report.copied_evidence[name] = display_path(target, report.repo_root)


def validate_evidence(report: RuntimeCaptureReport, *, min_mtime: float | None = None) -> None:
    base_dir = report.install_dir / report.base_game
    route_document = ROUTE_DOCUMENTS.get(report.route_id)
    rml_doc = base_dir / "ui" / "rml" / "core" / "runtime_smoke.rml"
    route_doc = base_dir / "ui" / "rml" / route_document if route_document else None
    log_text = read_text_if_exists(report.log_path)
    condump_text = read_text_if_exists(report.condump_path)
    combined_text = log_text + "\n" + condump_text
    dimensions = read_screenshot_dimensions(report.screenshot_path, report.screenshot_format)
    payload_nonzero = screenshot_payload_is_nonzero(
        report.screenshot_path,
        report.screenshot_format,
    )
    frame_values = parse_counter_line(combined_text, FRAMES_MARKER)
    route_counter_values = parse_counter_line(combined_text, ROUTE_COUNTERS_MARKER)
    input_values = parse_counter_line(combined_text, INPUT_MARKER)
    layout_facts: dict[str, Any] = {
        "layout_checked": False,
        "layout_ok": False,
        "layout_required": report.require_layout,
    }
    layout_errors: list[str] = []
    if report.screenshot_path.is_file():
        layout_facts, layout_errors = analyze_screenshot_layout(
            report.screenshot_path,
            report.screenshot_format,
        )
        if not report.require_layout:
            layout_facts["layout_required"] = False
            layout_facts["layout_ok"] = None
            layout_errors = []
        else:
            layout_facts["layout_required"] = True

    report.facts.update(
        {
            "engine_exe_exists": report.engine_exe.is_file(),
            "install_dir_exists": report.install_dir.is_dir(),
            "base_game_dir_exists": base_dir.is_dir(),
            "runtime_smoke_document_exists": rml_doc.is_file(),
            "route_id": report.route_id,
            "route_document": route_document.as_posix() if route_document else None,
            "route_document_exists": route_doc.is_file() if route_doc else False,
            "log_exists": report.log_path.is_file(),
            "condump_exists": report.condump_path.is_file(),
            "screenshot_exists": report.screenshot_path.is_file(),
            "screenshot_size": report.screenshot_path.stat().st_size
            if report.screenshot_path.is_file()
            else 0,
            "screenshot_format": report.screenshot_format,
            "screenshot_dimensions": dimensions,
            "viewport_name": report.viewport_name,
            "geometry": report.geometry,
            "expected_dimensions": report.expected_dimensions,
            "screenshot_payload_nonzero": payload_nonzero,
            "capture_marker_seen": CAPTURE_MARKER in combined_text
            or MENU_CAPTURE_MARKER in combined_text,
            "synthetic_input_marker_seen": SYNTHETIC_INPUT_MARKER in combined_text,
            "font_geometry_marker_seen": FONT_GEOMETRY_MARKER in combined_text,
            "font_q2r_source_marker_seen": FONT_Q2R_SOURCE_MARKER in combined_text,
            "guarded_opengl_status_seen": runtime_status_is_guarded_opengl(
                combined_text,
                report.route_id,
            ),
            "inactive_status_seen": runtime_status_inactive_seen(combined_text),
            "frames_marker_seen": FRAMES_MARKER in combined_text,
            "route_counters_marker_seen": ROUTE_COUNTERS_MARKER in combined_text,
            "input_marker_seen": INPUT_MARKER in combined_text,
            "frame_updates": frame_values.get("updates", 0),
            "frame_renders": frame_values.get("renders", 0),
            "route_opens": route_counter_values.get("opens", 0),
            "route_closes": route_counter_values.get("closes", 0),
            "route_close_requests": route_counter_values.get("close_requests", 0),
            "route_synthetic_inputs": route_counter_values.get("synthetic_inputs", 0),
            "input_keys": input_values.get("keys", 0),
            "input_chars": input_values.get("chars", 0),
            "input_mouse_moves": input_values.get("mouse_moves", 0),
            "input_mouse_buttons": input_values.get("mouse_buttons", 0),
            "input_mouse_wheels": input_values.get("mouse_wheels", 0),
            "screenshot_write_seen": SCREENSHOT_MARKER in combined_text
            and report.screenshot_path.name in combined_text,
            **layout_facts,
        }
    )

    if min_mtime is not None and report.screenshot_path.is_file():
        report.facts["screenshot_fresh"] = report.screenshot_path.stat().st_mtime >= min_mtime - 1
    elif min_mtime is not None:
        report.facts["screenshot_fresh"] = False

    required = (
        ("engine_exe_exists", f"engine executable missing: {report.engine_exe}"),
        ("install_dir_exists", f"install directory missing: {report.install_dir}"),
        ("base_game_dir_exists", f"base game directory missing: {base_dir}"),
        (
            "route_document_exists",
            f"RmlUi route document missing for {report.route_id}: {route_doc}",
        ),
        ("log_exists", f"runtime capture log missing: {report.log_path}"),
        ("screenshot_exists", f"runtime capture screenshot missing: {report.screenshot_path}"),
        ("capture_marker_seen", "runtime capture marker was not found in log/condump evidence"),
        ("synthetic_input_marker_seen", "runtime synthetic input marker was not found"),
        ("font_geometry_marker_seen", "RmlUi TTF font texture marker was not found"),
        ("font_q2r_source_marker_seen", "RmlUi Quake II Rerelease font source marker was not found"),
        ("guarded_opengl_status_seen", "guarded OpenGL active-route status was not found"),
        ("inactive_status_seen", "runtime inactive status after synthetic back-close was not found"),
        ("frames_marker_seen", "runtime frame counter line was not found"),
        ("route_counters_marker_seen", "runtime route counter line was not found"),
        ("input_marker_seen", "runtime input counter line was not found"),
        ("screenshot_write_seen", "screenshot write confirmation was not found"),
    )
    for fact, message in required:
        if not report.facts.get(fact):
            report.errors.append(message)

    if dimensions is None:
        report.errors.append(
            f"runtime capture screenshot is not a readable {report.screenshot_format.upper()}"
        )
    elif report.expected_dimensions is not None and dimensions != report.expected_dimensions:
        report.errors.append(
            "runtime capture screenshot dimensions "
            f"{dimensions[0]}x{dimensions[1]} did not match expected "
            f"{report.expected_dimensions[0]}x{report.expected_dimensions[1]}"
        )
    if payload_nonzero is False:
        report.errors.append("runtime capture screenshot payload is blank")

    for error in layout_errors:
        report.errors.append(f"runtime capture layout: {error}")

    if report.facts.get("frame_updates", 0) <= 0 or report.facts.get("frame_renders", 0) <= 0:
        report.errors.append("runtime frame counters did not record an update and render")

    input_requirements = (
        ("input_keys", "runtime synthetic input did not record key events"),
        ("input_chars", "runtime synthetic input did not record text input"),
        ("input_mouse_moves", "runtime synthetic input did not record pointer motion"),
        ("input_mouse_buttons", "runtime synthetic input did not record mouse-button input"),
        ("input_mouse_wheels", "runtime synthetic input did not record mouse-wheel input"),
    )
    for fact, message in input_requirements:
        if report.facts.get(fact, 0) <= 0:
            report.errors.append(message)

    route_counter_requirements = (
        ("route_opens", "runtime route counters did not record a route open"),
        ("route_closes", "runtime route counters did not record a route close"),
        ("route_close_requests", "runtime route counters did not record a close/back request"),
        ("route_synthetic_inputs", "runtime route counters did not record synthetic input"),
    )
    for fact, message in route_counter_requirements:
        if report.facts.get(fact, 0) <= 0:
            report.errors.append(message)

    if min_mtime is not None and not report.facts.get("screenshot_fresh"):
        report.errors.append("runtime capture screenshot was not freshly written by this run")


def write_manifest(report: RuntimeCaptureReport) -> None:
    if not report.manifest_path:
        return
    report.manifest_path.parent.mkdir(parents=True, exist_ok=True)
    report.manifest_path.write_text(
        json.dumps(json_report_payload(report), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def write_matrix_manifest(
    reports: list[RuntimeCaptureReport],
    manifest_path: Path | None,
    repo_root: Path,
) -> None:
    if not manifest_path:
        return
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(matrix_report_payload(reports, repo_root, manifest_path), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def write_route_matrix_manifest(
    reports: list[RuntimeCaptureReport],
    manifest_path: Path | None,
    repo_root: Path,
) -> None:
    if not manifest_path:
        return
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(route_matrix_report_payload(reports, repo_root, manifest_path), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def write_renderer_matrix_manifest(
    route_reports: list[RuntimeCaptureReport],
    renderer_report: renderer_matrix.RendererMatrixReport,
    bridge_report: bridge_readiness.BridgeReadinessReport,
    manifest_path: Path | None,
    repo_root: Path,
) -> None:
    if not manifest_path:
        return
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(
            runtime_renderer_matrix_payload(
                route_reports,
                renderer_report,
                bridge_report,
                repo_root,
                manifest_path,
            ),
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


def json_report_payload(report: RuntimeCaptureReport) -> dict[str, Any]:
    return {
        "ok": report.ok,
        "paths": {
            "repo_root": str(report.repo_root),
            "install_dir": display_path(report.install_dir, report.repo_root),
            "engine_exe": display_path(report.engine_exe, report.repo_root),
            "screenshot": display_path(report.screenshot_path, report.repo_root),
            "log": display_path(report.log_path, report.repo_root),
            "condump": display_path(report.condump_path, report.repo_root),
            "evidence_dir": display_path(report.evidence_dir, report.repo_root),
            "manifest": display_path(report.manifest_path, report.repo_root)
            if report.manifest_path
            else None,
        },
        "command": report.command,
        "ran_engine": report.ran_engine,
        "exit_code": report.exit_code,
        "elapsed_seconds": report.elapsed_seconds,
        "facts": report.facts,
        "copied_evidence": report.copied_evidence,
        "errors": report.errors,
    }


def matrix_report_payload(
    reports: list[RuntimeCaptureReport],
    repo_root: Path,
    manifest_path: Path | None = None,
) -> dict[str, Any]:
    errors = [
        {
            "viewport": report.viewport_name,
            "evidence_id": report.evidence_id,
            "error": error,
        }
        for report in reports
        for error in report.errors
    ]
    return {
        "ok": all(report.ok for report in reports),
        "paths": {
            "repo_root": str(repo_root),
            "manifest": display_path(manifest_path, repo_root) if manifest_path else None,
        },
        "counts": {
            "viewports": len(reports),
            "passed": sum(1 for report in reports if report.ok),
            "failed": sum(1 for report in reports if not report.ok),
            "errors": len(errors),
        },
        "viewports": [json_report_payload(report) for report in reports],
        "errors": errors,
    }


def route_matrix_report_payload(
    reports: list[RuntimeCaptureReport],
    repo_root: Path,
    manifest_path: Path | None = None,
) -> dict[str, Any]:
    errors = [
        {
            "route_id": report.route_id,
            "evidence_id": report.evidence_id,
            "error": error,
        }
        for report in reports
        for error in report.errors
    ]
    return {
        "ok": all(report.ok for report in reports),
        "paths": {
            "repo_root": str(repo_root),
            "manifest": display_path(manifest_path, repo_root) if manifest_path else None,
        },
        "counts": {
            "routes": len(reports),
            "passed": sum(1 for report in reports if report.ok),
            "failed": sum(1 for report in reports if not report.ok),
            "errors": len(errors),
        },
        "routes": [json_report_payload(report) for report in reports],
        "errors": errors,
    }


def build_renderer_guardrail_report(repo_root: Path) -> renderer_matrix.RendererMatrixReport:
    return renderer_matrix.validate_renderer_matrix(
        repo_root=repo_root,
        meson_build_path=renderer_matrix.resolve_path(
            renderer_matrix.DEFAULT_MESON_BUILD,
            repo_root,
        ),
        renderer_header_path=renderer_matrix.resolve_path(
            renderer_matrix.DEFAULT_RENDERER_HEADER,
            repo_root,
        ),
        renderer_api_source_path=renderer_matrix.resolve_path(
            renderer_matrix.DEFAULT_RENDERER_API_SOURCE,
            repo_root,
        ),
        renderer_bridge_source_path=renderer_matrix.resolve_path(
            renderer_matrix.DEFAULT_RENDERER_BRIDGE_SOURCE,
            repo_root,
        ),
        client_renderer_source_path=renderer_matrix.resolve_path(
            renderer_matrix.DEFAULT_CLIENT_RENDERER_SOURCE,
            repo_root,
        ),
        capture_checker_path=renderer_matrix.resolve_path(
            renderer_matrix.DEFAULT_CAPTURE_CHECKER,
            repo_root,
        ),
    )


def build_bridge_readiness_report(repo_root: Path) -> bridge_readiness.BridgeReadinessReport:
    return bridge_readiness.validate_bridge_readiness(
        repo_root=repo_root,
        paths=bridge_readiness.DEFAULT_INPUTS,
    )


def runtime_renderer_matrix_payload(
    route_reports: list[RuntimeCaptureReport],
    renderer_report: renderer_matrix.RendererMatrixReport,
    bridge_report: bridge_readiness.BridgeReadinessReport,
    repo_root: Path,
    manifest_path: Path | None = None,
) -> dict[str, Any]:
    route_payload = route_matrix_report_payload(route_reports, repo_root, manifest_path)
    renderer_payload = renderer_matrix.json_report_payload(renderer_report)
    bridge_payload = bridge_readiness.json_report_payload(bridge_report)
    route_errors = [
        {
            "source": "opengl_route_matrix",
            **error,
        }
        for error in route_payload["errors"]
    ]
    renderer_errors = [
        {
            "source": "renderer_guardrail",
            "error": error,
        }
        for error in renderer_payload["errors"]
    ]
    bridge_errors = [
        {
            "source": "bridge_readiness",
            "error": error,
        }
        for error in bridge_payload["errors"]
    ]
    errors = route_errors + renderer_errors + bridge_errors

    return {
        "ok": route_payload["ok"] and renderer_payload["ok"] and bridge_payload["ok"],
        "paths": {
            "repo_root": str(repo_root),
            "manifest": display_path(manifest_path, repo_root) if manifest_path else None,
        },
        "counts": {
            "routes": route_payload["counts"]["routes"],
            "route_passed": route_payload["counts"]["passed"],
            "route_failed": route_payload["counts"]["failed"],
            "renderer_lanes": renderer_payload["counts"]["lanes"],
            "native_guarded_lanes": renderer_payload["counts"]["native_guarded_lanes"],
            "blocked_lanes": renderer_payload["counts"]["blocked_lanes"],
            "bridge_lanes": bridge_payload["counts"]["lanes"],
            "bridge_foundation_lanes": bridge_payload["counts"]["foundation_lanes"],
            "native_bridge_lanes": bridge_payload["counts"]["native_bridge_lanes"],
            "bridge_blocked_lanes": bridge_payload["counts"]["blocked_lanes"],
            "bridge_activation_complete_lanes": bridge_payload["counts"]["activation_complete_lanes"],
            "bridge_partial_activation_lanes": bridge_payload["counts"]["partial_activation_lanes"],
            "bridge_inactive_activation_lanes": bridge_payload["counts"]["inactive_activation_lanes"],
            "bridge_activation_requirements": bridge_payload["counts"]["activation_requirements"],
            "bridge_satisfied_activation_requirements": bridge_payload["counts"]["satisfied_activation_requirements"],
            "bridge_pending_activation_requirements": bridge_payload["counts"]["pending_activation_requirements"],
            "missing_bridge_requirements": bridge_payload["counts"]["missing_bridge_requirements"],
            "errors": len(errors),
        },
        "opengl_route_matrix": route_payload,
        "renderer_guardrail": renderer_payload,
        "bridge_readiness": bridge_payload,
        "errors": errors,
    }


def print_report(report: RuntimeCaptureReport) -> None:
    yes_no = lambda value: "yes" if value else "no"

    print("RmlUi runtime capture smoke:")
    if report.viewport_name:
        print(f"  Viewport: {report.viewport_name}")
    if report.geometry:
        print(f"  Geometry: {report.geometry}")
    print(f"  Engine executable: {display_path(report.engine_exe, report.repo_root)}")
    print(f"  Install directory: {display_path(report.install_dir, report.repo_root)}")
    print(f"  Screenshot: {display_path(report.screenshot_path, report.repo_root)}")
    print(f"  Log: {display_path(report.log_path, report.repo_root)}")
    print(f"  Condump: {display_path(report.condump_path, report.repo_root)}")
    print(f"  Ran engine: {yes_no(report.ran_engine)}")
    if report.exit_code is not None:
        print(f"  Engine exit code: {report.exit_code}")
    print(f"  Capture marker: {yes_no(report.facts.get('capture_marker_seen'))}")
    print(f"  Synthetic input marker: {yes_no(report.facts.get('synthetic_input_marker_seen'))}")
    print(f"  TTF font texture: {yes_no(report.facts.get('font_geometry_marker_seen'))}")
    print(f"  Q2R font source: {yes_no(report.facts.get('font_q2r_source_marker_seen'))}")
    print(f"  Guarded OpenGL status: {yes_no(report.facts.get('guarded_opengl_status_seen'))}")
    print(f"  Inactive close status: {yes_no(report.facts.get('inactive_status_seen'))}")
    print(f"  Frame counters: {yes_no(report.facts.get('frames_marker_seen'))}")
    print(f"  Route counters: {yes_no(report.facts.get('route_counters_marker_seen'))}")
    print(f"  Input counters: {yes_no(report.facts.get('input_marker_seen'))}")
    print(f"  Screenshot written: {yes_no(report.facts.get('screenshot_write_seen'))}")
    print(f"  Layout assertions: {yes_no(report.facts.get('layout_ok'))}")
    print(f"  Screenshot format: {report.screenshot_format.upper()}")
    print(f"  Screenshot dimensions: {report.facts.get('screenshot_dimensions') or '-'}")
    if report.copied_evidence:
        print("  Copied evidence:")
        for name, path in sorted(report.copied_evidence.items()):
            print(f"    {name}: {path}")
    if report.errors:
        print("\nFindings:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi runtime capture smoke failed.")
    else:
        print("\nResult: RmlUi runtime capture smoke passed.")


def print_matrix_report(reports: list[RuntimeCaptureReport]) -> None:
    print("RmlUi runtime capture viewport matrix:")
    for report in reports:
        dimensions = report.facts.get("screenshot_dimensions") or "-"
        result = "passed" if report.ok else "failed"
        print(
            f"  {report.viewport_name or report.evidence_id}: {result}; "
            f"geometry={report.geometry or '-'}; dimensions={dimensions}"
        )
        for error in report.errors:
            print(f"    - {error}")

    if all(report.ok for report in reports):
        print("\nResult: RmlUi runtime capture viewport matrix passed.")
    else:
        print("\nResult: RmlUi runtime capture viewport matrix failed.")


def print_route_matrix_report(reports: list[RuntimeCaptureReport]) -> None:
    print("RmlUi runtime capture route matrix:")
    for report in reports:
        dimensions = report.facts.get("screenshot_dimensions") or "-"
        result = "passed" if report.ok else "failed"
        print(
            f"  {report.route_id}: {result}; "
            f"geometry={report.geometry or '-'}; dimensions={dimensions}"
        )
        for error in report.errors:
            print(f"    - {error}")

    if all(report.ok for report in reports):
        print("\nResult: RmlUi runtime capture route matrix passed.")
    else:
        print("\nResult: RmlUi runtime capture route matrix failed.")


def print_runtime_renderer_matrix_report(
    route_reports: list[RuntimeCaptureReport],
    renderer_report: renderer_matrix.RendererMatrixReport,
    bridge_report: bridge_readiness.BridgeReadinessReport,
) -> None:
    print("RmlUi runtime capture renderer matrix:")

    renderer_result = "passed" if renderer_report.ok() else "failed"
    print(f"  Renderer guardrail: {renderer_result}")
    for lane in renderer_report.lanes.values():
        lane_result = "passed" if lane.ok() else "failed"
        print(f"    {lane.label}: {lane.expected_status}; {lane_result}")
        for error in lane.errors:
            print(f"      - {error}")

    bridge_result = "passed" if bridge_report.ok() else "failed"
    print(f"  Bridge readiness: {bridge_result}")
    for lane in bridge_report.lanes.values():
        lane_result = "passed" if lane.ok() else "failed"
        foundation_result = "foundation=yes" if lane.foundation_ok() else "foundation=no"
        native_result = "native=yes" if lane.native_bridge_claimed() else "native=no"
        print(
            f"    {lane.label}: {lane.expected_status}; "
            f"{foundation_result}; {native_result}; {lane_result}"
        )
        for error in lane.errors:
            print(f"      - {error}")

    print("  OpenGL route matrix:")
    for report in route_reports:
        dimensions = report.facts.get("screenshot_dimensions") or "-"
        result = "passed" if report.ok else "failed"
        print(
            f"    {report.route_id}: {result}; "
            f"geometry={report.geometry or '-'}; dimensions={dimensions}"
        )
        for error in report.errors:
            print(f"      - {error}")

    if all(report.ok for report in route_reports) and renderer_report.ok() and bridge_report.ok():
        print("\nResult: RmlUi runtime capture renderer matrix passed.")
    else:
        print("\nResult: RmlUi runtime capture renderer matrix failed.")


def build_report(
    repo_root: Path,
    install_dir: Path,
    engine_exe: Path,
    base_game: str,
    evidence_dir: Path,
    evidence_id: str,
    screenshot_format: str,
    route_id: str,
    require_layout: bool,
    viewport_name: str | None,
    geometry: str | None,
    startup_wait: int,
    capture_wait: int,
    status_wait: int,
    manifest_path: Path | None,
) -> RuntimeCaptureReport:
    command = build_engine_command(
        engine_exe,
        install_dir,
        base_game,
        evidence_id,
        screenshot_format,
        route_id,
        geometry,
        startup_wait,
        capture_wait,
        status_wait,
    )
    base_dir = install_dir / base_game
    return RuntimeCaptureReport(
        repo_root=repo_root,
        install_dir=install_dir,
        engine_exe=engine_exe,
        base_game=base_game,
        evidence_dir=evidence_dir,
        evidence_id=evidence_id,
        screenshot_format=screenshot_format,
        route_id=route_id,
        require_layout=require_layout,
        viewport_name=viewport_name,
        geometry=geometry,
        expected_dimensions=parse_geometry_dimensions(geometry),
        command=command,
        screenshot_path=base_dir / "screenshots" / f"{evidence_id}{screenshot_extension(screenshot_format)}",
        log_path=base_dir / "logs" / f"{evidence_id}.log",
        condump_path=base_dir / "condumps" / f"{evidence_id}.txt",
        manifest_path=manifest_path,
    )


def run_engine_for_report(report: RuntimeCaptureReport, timeout: float) -> float | None:
    if not report.engine_exe.is_file():
        report.errors.append(f"engine executable missing: {report.engine_exe}")
        return None

    start_time = time.time()
    completed = subprocess.run(
        report.command,
        cwd=report.engine_exe.parent,
        env=build_run_environment(report.engine_exe),
        timeout=timeout,
        check=False,
    )
    report.ran_engine = True
    report.exit_code = completed.returncode
    report.elapsed_seconds = time.time() - start_time
    if completed.returncode != 0:
        report.errors.append(f"engine exited with code {completed.returncode}")

    return start_time


def build_matrix_reports(
    repo_root: Path,
    install_dir: Path,
    engine_exe: Path,
    base_game: str,
    evidence_dir: Path,
    evidence_id: str,
    screenshot_format: str,
    startup_wait: int,
    capture_wait: int,
    status_wait: int,
) -> list[RuntimeCaptureReport]:
    return [
        build_report(
            repo_root,
            install_dir,
            engine_exe,
            base_game,
            evidence_dir,
            f"{evidence_id}_{name}",
            screenshot_format,
            DEFAULT_ROUTE_ID,
            True,
            name,
            geometry,
            startup_wait,
            capture_wait,
            status_wait,
            None,
        )
        for name, geometry in DEFAULT_VIEWPORT_MATRIX
    ]


def build_route_reports(
    repo_root: Path,
    install_dir: Path,
    engine_exe: Path,
    base_game: str,
    evidence_dir: Path,
    evidence_id: str,
    screenshot_format: str,
    startup_wait: int,
    capture_wait: int,
    status_wait: int,
) -> list[RuntimeCaptureReport]:
    return [
        build_report(
            repo_root,
            install_dir,
            engine_exe,
            base_game,
            evidence_dir,
            f"{evidence_id}_{route_id}",
            screenshot_format,
            route_id,
            False,
            None,
            DEFAULT_ROUTE_MATRIX_GEOMETRY,
            startup_wait,
            capture_wait,
            status_wait,
            None,
        )
        for route_id in DEFAULT_ROUTE_MATRIX
    ]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--install-dir", type=Path, default=DEFAULT_INSTALL_DIR)
    parser.add_argument("--engine-exe", type=Path, default=DEFAULT_ENGINE_EXE)
    parser.add_argument("--base-game", default=DEFAULT_BASE_GAME)
    parser.add_argument("--evidence-dir", type=Path, default=DEFAULT_EVIDENCE_DIR)
    parser.add_argument("--evidence-id", default=DEFAULT_EVIDENCE_ID)
    parser.add_argument("--screenshot-format", choices=("tga", "png"), default=DEFAULT_SCREENSHOT_FORMAT)
    parser.add_argument("--route-id", default=DEFAULT_ROUTE_ID, choices=tuple(ROUTE_DOCUMENTS))
    parser.add_argument("--geometry", default=None, help="Windowed capture geometry as WIDTHxHEIGHT.")
    parser.add_argument("--matrix", action="store_true", help="Run or validate the default viewport matrix.")
    parser.add_argument("--route-matrix", action="store_true", help="Run or validate the guarded menu route matrix.")
    parser.add_argument(
        "--renderer-matrix",
        action="store_true",
        help="Run or validate the guarded OpenGL route matrix plus renderer-family guardrails.",
    )
    parser.add_argument("--startup-wait", type=int, default=DEFAULT_STARTUP_WAIT)
    parser.add_argument("--capture-wait", type=int, default=DEFAULT_CAPTURE_WAIT)
    parser.add_argument("--status-wait", type=int, default=DEFAULT_STATUS_WAIT)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--run", action="store_true", help="Launch the engine before validating evidence.")
    parser.add_argument("--dry-run", action="store_true", help="Print the command and skip evidence validation.")
    parser.add_argument("--write-manifest", type=Path, default=None)
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    install_dir = resolve_path(args.install_dir, repo_root)
    engine_exe = resolve_path(args.engine_exe, repo_root)
    evidence_dir = resolve_path(args.evidence_dir, repo_root)
    manifest_path = resolve_path(args.write_manifest, repo_root) if args.write_manifest else None

    if args.geometry:
        try:
            parse_geometry_dimensions(args.geometry)
        except ValueError as exc:
            parser.error(str(exc))

    matrix_modes = [args.matrix, args.route_matrix, args.renderer_matrix]
    if sum(1 for enabled in matrix_modes if enabled) > 1:
        parser.error("--matrix, --route-matrix, and --renderer-matrix cannot be combined")

    if args.matrix:
        reports = build_matrix_reports(
            repo_root,
            install_dir,
            engine_exe,
            args.base_game,
            evidence_dir,
            args.evidence_id,
            args.screenshot_format,
            args.startup_wait,
            args.capture_wait,
            args.status_wait,
        )

        if args.dry_run:
            if args.format == "json":
                print(json.dumps(matrix_report_payload(reports, repo_root, manifest_path), indent=2, sort_keys=True))
            else:
                for report in reports:
                    print(" ".join(report.command))
            return 0

        for report in reports:
            start_time = run_engine_for_report(report, args.timeout) if args.run else None
            validate_evidence(report, min_mtime=start_time if args.run else None)
            if report.ok:
                collect_evidence(report)

        write_matrix_manifest(reports, manifest_path, repo_root)

        if args.format == "json":
            print(json.dumps(matrix_report_payload(reports, repo_root, manifest_path), indent=2, sort_keys=True))
        else:
            print_matrix_report(reports)

        return 0 if all(report.ok for report in reports) else 1

    if args.route_matrix:
        reports = build_route_reports(
            repo_root,
            install_dir,
            engine_exe,
            args.base_game,
            evidence_dir,
            args.evidence_id,
            args.screenshot_format,
            args.startup_wait,
            args.capture_wait,
            args.status_wait,
        )

        if args.dry_run:
            if args.format == "json":
                print(json.dumps(route_matrix_report_payload(reports, repo_root, manifest_path), indent=2, sort_keys=True))
            else:
                for report in reports:
                    print(" ".join(report.command))
            return 0

        for report in reports:
            start_time = run_engine_for_report(report, args.timeout) if args.run else None
            validate_evidence(report, min_mtime=start_time if args.run else None)
            if report.ok:
                collect_evidence(report)

        write_route_matrix_manifest(reports, manifest_path, repo_root)

        if args.format == "json":
            print(json.dumps(route_matrix_report_payload(reports, repo_root, manifest_path), indent=2, sort_keys=True))
        else:
            print_route_matrix_report(reports)

        return 0 if all(report.ok for report in reports) else 1

    if args.renderer_matrix:
        reports = build_route_reports(
            repo_root,
            install_dir,
            engine_exe,
            args.base_game,
            evidence_dir,
            args.evidence_id,
            args.screenshot_format,
            args.startup_wait,
            args.capture_wait,
            args.status_wait,
        )
        renderer_report = build_renderer_guardrail_report(repo_root)
        bridge_report = build_bridge_readiness_report(repo_root)

        if args.dry_run:
            if args.format == "json":
                print(
                    json.dumps(
                        runtime_renderer_matrix_payload(
                            reports,
                            renderer_report,
                            bridge_report,
                            repo_root,
                            manifest_path,
                        ),
                        indent=2,
                        sort_keys=True,
                    )
                )
            else:
                for report in reports:
                    print(" ".join(report.command))
                print(
                    "# Renderer guardrail lanes: "
                    "OpenGL=native_guarded, "
                    "Vulkan=blocked_until_native, "
                    "RTX/vkpt=blocked_until_native"
                )
                print(
                    "# Bridge readiness lanes: "
                    "Vulkan=foundation_present_blocked, "
                    "RTX/vkpt=foundation_present_blocked"
                )
            return 0 if renderer_report.ok() and bridge_report.ok() else 1

        for report in reports:
            start_time = run_engine_for_report(report, args.timeout) if args.run else None
            validate_evidence(report, min_mtime=start_time if args.run else None)
            if report.ok:
                collect_evidence(report)

        write_renderer_matrix_manifest(
            reports,
            renderer_report,
            bridge_report,
            manifest_path,
            repo_root,
        )

        if args.format == "json":
            print(
                json.dumps(
                        runtime_renderer_matrix_payload(
                            reports,
                            renderer_report,
                            bridge_report,
                            repo_root,
                            manifest_path,
                        ),
                    indent=2,
                    sort_keys=True,
                )
            )
        else:
            print_runtime_renderer_matrix_report(reports, renderer_report, bridge_report)

        return (
            0
            if all(report.ok for report in reports)
            and renderer_report.ok()
            and bridge_report.ok()
            else 1
        )

    report = build_report(
        repo_root,
        install_dir,
        engine_exe,
        args.base_game,
        evidence_dir,
        args.evidence_id,
        args.screenshot_format,
        args.route_id,
        args.route_id == DEFAULT_ROUTE_ID,
        None,
        args.geometry,
        args.startup_wait,
        args.capture_wait,
        args.status_wait,
        manifest_path,
    )

    if args.dry_run:
        report.facts["dry_run"] = True
        if args.format == "json":
            print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))
        else:
            print(" ".join(report.command))
        return 0

    start_time = run_engine_for_report(report, args.timeout) if args.run else None

    validate_evidence(report, min_mtime=start_time if args.run else None)
    if report.ok:
        collect_evidence(report)
    write_manifest(report)

    if args.format == "json":
        print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))
    else:
        print_report(report)

    return 0 if report.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
