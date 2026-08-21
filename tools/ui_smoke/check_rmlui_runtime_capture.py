"""Run or validate guarded RmlUi runtime screenshot and navigation evidence."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import stat
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
DEFAULT_RENDERER = "opengl"
RENDERER_FAMILIES = {
    "opengl": "opengl",
    "vulkan": "vulkan",
    "rtx": "rtx_vkpt",
}
RENDERER_LABELS = {
    "opengl": "OpenGL",
    "vulkan": "Vulkan",
    "rtx": "RTX/vkpt",
}
DEFAULT_EVIDENCE_DIR = Path(".tmp/rmlui/runtime-capture")
DEFAULT_NAVIGATION_RUNTIME_DIR = Path(".tmp/rmlui/runtime-navigation")
DEFAULT_EVIDENCE_ID = "rmlui_runtime_smoke_round29"
DEFAULT_STARTUP_WAIT = 120
# Leave enough frames for route providers, cvar-bound images, and renderer
# uploads to settle before recording deterministic visual evidence.
DEFAULT_CAPTURE_WAIT = 360
DEFAULT_STATUS_WAIT = 12
MAX_NAVIGATION_TIMEOUT = 300.0
DEFAULT_SCREENSHOT_FORMAT = "tga"
DEFAULT_VIEWPORT_MATRIX = (
    ("default_960x720", "960x720"),
    ("large_1280x960", "1280x960"),
)
DEFAULT_ROUTE_MATRIX = ("main", "game", "download_status")
MENU_ENTRYPOINT_ROUTES = frozenset(DEFAULT_ROUTE_MATRIX)
DEFAULT_ROUTE_MATRIX_GEOMETRY = "960x720"
DEFAULT_ROUTE_ID = "core.runtime_smoke"
NAVIGATION_JOURNEY_ID = "main_options_video_back"
NAVIGATION_CHECKPOINT_MARKER = "RMLUI_NAV_CHECKPOINT"
NAVIGATION_EVIDENCE_SCHEMA = "worr.rmlui.runtime_navigation.v1"
NAVIGATION_BASE_GAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")
NAVIGATION_EVIDENCE_BASE_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,95}$")
NAVIGATION_EVIDENCE_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")
NAVIGATION_RENDERERS = tuple(RENDERER_FAMILIES)
NAVIGATION_CHECKPOINTS = (
    ("main", True, "main"),
    ("options", True, "options"),
    ("video", True, "video"),
    ("back_options", True, "options"),
    ("back_main", True, "main"),
    ("inactive", False, "<none>"),
)

ROUTE_DOCUMENTS = {
    "core.runtime_smoke": Path("core/runtime_smoke.rml"),
    "main": Path("shell/main.rml"),
    "game": Path("shell/game.rml"),
    "download_status": Path("shell/download_status.rml"),
    "downloads": Path("shell/downloads.rml"),
    "play": Path("shell/play.rml"),
    "options": Path("shell/options.rml"),
    "quit_confirm": Path("shell/quit_confirm.rml"),
    "video": Path("settings/video.rml"),
    "multimonitor": Path("settings/multimonitor.rml"),
    "performance": Path("settings/performance.rml"),
    "accessibility": Path("settings/accessibility.rml"),
    "sound": Path("settings/sound.rml"),
    "railtrail": Path("settings/railtrail.rml"),
    "effects": Path("settings/effects.rml"),
    "crosshair": Path("settings/crosshair.rml"),
    "screen": Path("settings/screen.rml"),
    "language": Path("settings/language.rml"),
    "input": Path("settings/input.rml"),
    "gameflags": Path("singleplayer/gameflags.rml"),
    "startserver": Path("singleplayer/startserver.rml"),
    "singleplayer": Path("singleplayer/singleplayer.rml"),
    "skill_select": Path("singleplayer/skill_select.rml"),
    "multiplayer": Path("multiplayer/multiplayer.rml"),
    "loadgame": Path("singleplayer/loadgame.rml"),
    "savegame": Path("singleplayer/savegame.rml"),
    "demos": Path("utility/demos.rml"),
    "servers": Path("utility/servers.rml"),
    "ui_list": Path("utility/ui_list.rml"),
    "players": Path("utility/players.rml"),
    "addressbook": Path("utility/addressbook.rml"),
    "keys": Path("utility/keys.rml"),
    "legacykeys": Path("utility/legacykeys.rml"),
    "weapons": Path("utility/weapons.rml"),
    "dm_welcome": Path("session/dm_welcome.rml"),
    "dm_join": Path("session/dm_join.rml"),
    "join": Path("session/join.rml"),
    "dm_hostinfo": Path("session/dm_hostinfo.rml"),
    "dm_matchinfo": Path("session/dm_matchinfo.rml"),
    "vote_menu": Path("session/vote_menu.rml"),
    "callvote_main": Path("session/callvote_main.rml"),
    "callvote_ruleset": Path("session/callvote_ruleset.rml"),
    "callvote_timelimit": Path("session/callvote_timelimit.rml"),
    "callvote_scorelimit": Path("session/callvote_scorelimit.rml"),
    "callvote_unlagged": Path("session/callvote_unlagged.rml"),
    "callvote_random": Path("session/callvote_random.rml"),
    "callvote_map_flags": Path("session/callvote_map_flags.rml"),
    "mymap_main": Path("session/mymap_main.rml"),
    "mymap_flags": Path("session/mymap_flags.rml"),
    "forfeit_confirm": Path("session/forfeit_confirm.rml"),
    "leave_match_confirm": Path("session/leave_match_confirm.rml"),
    "admin_menu": Path("session/admin_menu.rml"),
    "admin_commands": Path("session/admin_commands.rml"),
    "tourney_info": Path("session/tourney_info.rml"),
    "tourney_mapchoices": Path("session/tourney_mapchoices.rml"),
    "tourney_veto": Path("session/tourney_veto.rml"),
    "tourney_replay_confirm": Path("session/tourney_replay_confirm.rml"),
    "map_selector": Path("session/map_selector.rml"),
    "match_stats": Path("session/match_stats.rml"),
}

# The migration manifest owns the 58 end-user routes. Keep the smoke-only
# document out of the full route matrix while preserving registry order so
# contact sheets and evidence manifests remain stable between runs.
FULL_ROUTE_MATRIX = tuple(
    route_id for route_id in ROUTE_DOCUMENTS if route_id != "core.runtime_smoke"
)

# Match-hub Back is intentionally disabled during the mandatory first-connect
# team/spectator choice. Full-route parity evidence exercises the resumable
# in-match state, where Escape and the Back plate must dismiss the hub.
ROUTE_SEED_CVARS: dict[str, tuple[tuple[str, str], ...]] = {
    "dm_welcome": (
        ("ui_welcome_hostname", "Local Session"),
        ("ui_welcome_motd", "Review the server message, then continue."),
    ),
    "dm_join": (("ui_dm_initial", "0"), ("ui_dm_show_resume", "1")),
    "join": (("ui_dm_initial", "0"), ("ui_dm_show_resume", "1")),
    "ui_list": (
        ("ui_list_state", "empty"),
        ("ui_list_status", "No entries are available for this session."),
    ),
}

SEED_CVAR_NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,63}$")

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
TGA_HEADER_SIZE = 18

STATUS_MARKER = "RmlUi runtime status:"
FRAMES_MARKER = "RmlUi runtime frames:"
ROUTE_COUNTERS_MARKER = "RmlUi runtime route counters:"
INPUT_MARKER = "RmlUi runtime input:"
CAPTURE_MARKER = "RmlUi guarded capture route is active"
MENU_CAPTURE_MARKER = "RmlUi guarded menu capture route"
ROUTE_CAPTURE_MARKER = "RmlUi guarded route capture"
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
    # The smoke document uses fixed-pixel text geometry, so its exact interior
    # color count does not grow with the viewport. Presence and placement are
    # additionally guarded by body_text_spans_summary_and_panel below.
    "body_text": (0.0, 1024),
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
    renderer: str
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


@dataclass
class NavigationJourneyReport:
    repo_root: Path
    install_dir: Path
    engine_exe: Path
    base_game: str
    renderer: str
    evidence_dir: Path
    runtime_home: Path
    evidence_id: str
    geometry: str
    command: list[str]
    log_path: Path
    manifest_path: Path
    ran_engine: bool = False
    exit_code: int | None = None
    elapsed_seconds: float | None = None
    copied_evidence: dict[str, str] = field(default_factory=dict)
    facts: dict[str, Any] = field(default_factory=dict)
    checkpoints: list[dict[str, Any]] = field(default_factory=list)
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


def runtime_status_is_guarded_renderer(
    text: str,
    route_id: str,
    renderer_family: str,
) -> bool:
    for line in text.splitlines():
        if STATUS_MARKER not in line:
            continue
        if (
            "active=yes" in line
            and f"route='{route_id}'" in line
            and f"family='{renderer_family}'" in line
        ):
            return True
    return False


def runtime_status_inactive_seen(text: str) -> bool:
    for line in text.splitlines():
        if STATUS_MARKER in line and "active=no" in line:
            return True
    return False


def parse_seed_cvar(value: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("seed cvars must use NAME=VALUE")

    name, cvar_value = value.split("=", 1)
    if not SEED_CVAR_NAME_PATTERN.fullmatch(name):
        raise argparse.ArgumentTypeError(
            "seed cvar names must begin with a letter or underscore and contain "
            "only letters, digits, or underscores"
        )
    if len(cvar_value) > 1024:
        raise argparse.ArgumentTypeError("seed cvar values are limited to 1024 characters")
    if any(character in cvar_value for character in ("\x00", "\r", "\n")):
        raise argparse.ArgumentTypeError("seed cvar values cannot contain control newlines")
    return name, cvar_value


def build_headless_engine_prefix(
    engine_exe: Path,
    install_dir: Path,
    base_game: str,
    renderer: str,
    evidence_id: str,
    geometry: str | None,
    startup_wait: int,
    screenshot_dir: Path | None,
    runtime_home: Path | None = None,
) -> list[str]:
    command = [
        str(engine_exe),
        "--bootstrap-skip-update-check",
        "--bootstrap-quiet-status",
        "+set",
        "basedir",
        str(install_dir),
    ]
    if runtime_home is not None:
        command.extend(["+set", "homedir", str(runtime_home)])
    command.extend(
        [
            "+set",
            "game",
            base_game,
            "+set",
            "win_headless",
            "1",
            "+set",
            "in_enable",
            "0",
            "+set",
            "in_grab",
            "0",
            "+set",
            "s_enable",
            "0",
            "+set",
            "r_renderer",
            renderer,
            "+set",
            "r_fullscreen",
            "0",
            "+set",
            "r_maxfps",
            "60",
        ]
    )

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
            "ui_rml_reduced_motion",
            "1",
        ]
    )
    if screenshot_dir is not None:
        command.extend(
            [
                "+set",
                "r_screenshot_async",
                "0",
                "+set",
                "r_screenshot_message",
                "1",
                "+set",
                "r_screenshot_dir",
                str(screenshot_dir),
            ]
        )
    command.extend(
        [
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
            # Config files may restore the archived cvar after command-line
            # +set processing. Execute the cvar command here, immediately
            # before opening a document, so reduced-motion evidence is real.
            "+ui_rml_reduced_motion",
            "1",
        ]
    )
    return command


def build_engine_command(
    engine_exe: Path,
    install_dir: Path,
    base_game: str,
    renderer: str,
    evidence_id: str,
    screenshot_format: str,
    route_id: str,
    geometry: str | None,
    startup_wait: int,
    capture_wait: int,
    status_wait: int,
    server_address: str | None = None,
    seed_cvars: tuple[tuple[str, str], ...] = (),
) -> list[str]:
    command = build_headless_engine_prefix(
        engine_exe,
        install_dir,
        base_game,
        renderer,
        evidence_id,
        geometry,
        startup_wait,
        install_dir / base_game / "screenshots",
    )
    for name, value in seed_cvars:
        command.extend(["+set", name, value])
    if route_id == "servers":
        # Keep guarded visual evidence deterministic and offline-capable even
        # when a user's config persisted the public HTTP source.
        if server_address:
            command.extend(["+set", "adr0", server_address])
        command.extend(["+set", "ui_server_source", "local"])
    if route_id == DEFAULT_ROUTE_ID:
        command.append("+ui_rml_runtime_capture")
    elif route_id in MENU_ENTRYPOINT_ROUTES:
        command.extend(
            [
                "+ui_rml_runtime_capture_menu",
                route_id,
            ]
        )
    else:
        command.extend(
            [
                "+ui_rml_runtime_capture_route",
                route_id,
            ]
        )

    command.extend(
        [
            "+wait",
            str(capture_wait),
            screenshot_command(screenshot_format),
            evidence_id,
        ]
    )
    command.extend(
        [
            "+wait",
            str(status_wait),
            "+ui_rml_runtime_synthetic_input",
            "+wait",
            str(status_wait),
            "+ui_rml_runtime_status",
            "+ui_rml_reduced_motion",
            "0",
            "+quit",
        ]
    )
    return command


def navigation_checkpoint_token(name: str) -> str:
    return f"{NAVIGATION_CHECKPOINT_MARKER}:{name}"


def build_navigation_engine_command(
    engine_exe: Path,
    install_dir: Path,
    runtime_home: Path,
    base_game: str,
    renderer: str,
    evidence_id: str,
    geometry: str,
    startup_wait: int,
    status_wait: int,
) -> list[str]:
    command = build_headless_engine_prefix(
        engine_exe,
        install_dir,
        base_game,
        renderer,
        evidence_id,
        geometry,
        startup_wait,
        None,
        runtime_home,
    )
    steps = (
        (("+ui_rml_runtime_capture_menu", "main"), "main"),
        (("+ui_rml_runtime_open", "options"), "options"),
        (("+ui_rml_runtime_open", "video"), "video"),
        (("+ui_rml_runtime_back",), "back_options"),
        (("+ui_rml_runtime_back",), "back_main"),
        (("+ui_rml_runtime_back",), "inactive"),
    )
    for action, checkpoint in steps:
        command.extend(action)
        command.extend(
            [
                "+wait",
                str(status_wait),
                "+echo",
                navigation_checkpoint_token(checkpoint),
                "+ui_rml_runtime_status",
            ]
        )
    command.extend(["+ui_rml_reduced_motion", "0", "+quit"])
    return command


def parse_runtime_status_line(line: str) -> dict[str, Any]:
    active_match = re.search(r"\bactive=(yes|no)\b", line)
    quoted_values = dict(re.findall(r"\b([a-z_]+)='([^']*)'", line))
    return {
        "active": active_match.group(1) == "yes" if active_match else None,
        "route": quoted_values.get("route"),
        "availability": quoted_values.get("availability"),
        "runtime": quoted_values.get("runtime"),
        "renderer_name": quoted_values.get("renderer"),
        "renderer_family": quoted_values.get("family"),
    }


def navigation_checkpoint_name(line: str) -> str | None:
    stripped = line.rstrip()
    for name, _active, _route in NAVIGATION_CHECKPOINTS:
        if stripped.endswith(navigation_checkpoint_token(name)):
            return name
    return None


def parse_navigation_checkpoints(
    text: str,
) -> tuple[list[str], list[dict[str, Any]]]:
    checkpoints = {
        name: {
            "name": name,
            "expected_active": expected_active,
            "expected_route": expected_route,
            "marker_count": 0,
            "status_count": 0,
            "frames_count": 0,
            "route_counters_count": 0,
            "input_count": 0,
            "active": None,
            "route": None,
            "availability": None,
            "runtime": None,
            "renderer_name": None,
            "renderer_family": None,
            "frame_updates": None,
            "frame_renders": None,
            "route_opens": None,
            "route_closes": None,
            "route_close_requests": None,
            "route_synthetic_inputs": None,
            "input_keys": None,
            "input_chars": None,
            "input_mouse_moves": None,
            "input_mouse_buttons": None,
            "input_mouse_wheels": None,
        }
        for name, expected_active, expected_route in NAVIGATION_CHECKPOINTS
    }
    observed_order: list[str] = []
    current: dict[str, Any] | None = None

    for line in text.splitlines():
        checkpoint_name = navigation_checkpoint_name(line)
        if checkpoint_name is not None:
            observed_order.append(checkpoint_name)
            current = checkpoints[checkpoint_name]
            current["marker_count"] += 1
            continue
        if current is None:
            continue
        if STATUS_MARKER in line:
            current["status_count"] += 1
            if current["status_count"] == 1:
                current.update(parse_runtime_status_line(line))
        elif FRAMES_MARKER in line:
            current["frames_count"] += 1
            if current["frames_count"] == 1:
                values = parse_counter_line(line, FRAMES_MARKER)
                current["frame_updates"] = values.get("updates")
                current["frame_renders"] = values.get("renders")
        elif ROUTE_COUNTERS_MARKER in line:
            current["route_counters_count"] += 1
            if current["route_counters_count"] == 1:
                values = parse_counter_line(line, ROUTE_COUNTERS_MARKER)
                current["route_opens"] = values.get("opens")
                current["route_closes"] = values.get("closes")
                current["route_close_requests"] = values.get("close_requests")
                current["route_synthetic_inputs"] = values.get("synthetic_inputs")
        elif INPUT_MARKER in line:
            current["input_count"] += 1
            if current["input_count"] == 1:
                values = parse_counter_line(line, INPUT_MARKER)
                current["input_keys"] = values.get("keys")
                current["input_chars"] = values.get("chars")
                current["input_mouse_moves"] = values.get("mouse_moves")
                current["input_mouse_buttons"] = values.get("mouse_buttons")
                current["input_mouse_wheels"] = values.get("mouse_wheels")

    return observed_order, [
        checkpoints[name] for name, _active, _route in NAVIGATION_CHECKPOINTS
    ]


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
            "capture_marker_seen": (
                CAPTURE_MARKER in combined_text
                if report.route_id == DEFAULT_ROUTE_ID
                else (
                    f"{MENU_CAPTURE_MARKER} '{report.route_id}'" in combined_text
                    if report.route_id in MENU_ENTRYPOINT_ROUTES
                    else f"{ROUTE_CAPTURE_MARKER} '{report.route_id}'" in combined_text
                )
            ),
            "synthetic_input_marker_seen": SYNTHETIC_INPUT_MARKER in combined_text,
            "font_geometry_marker_seen": FONT_GEOMETRY_MARKER in combined_text,
            "font_q2r_source_marker_seen": FONT_Q2R_SOURCE_MARKER in combined_text,
            "renderer": report.renderer,
            "guarded_renderer_status_seen": runtime_status_is_guarded_renderer(
                combined_text,
                report.route_id,
                RENDERER_FAMILIES[report.renderer],
            ),
            "guarded_opengl_status_seen": runtime_status_is_guarded_renderer(
                combined_text,
                report.route_id,
                RENDERER_FAMILIES["opengl"],
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
        (
            "guarded_renderer_status_seen",
            f"guarded {RENDERER_LABELS[report.renderer]} active-route status was not found",
        ),
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


def command_contains_sequence(command: list[str], sequence: tuple[str, ...]) -> bool:
    width = len(sequence)
    return any(
        tuple(command[index : index + width]) == sequence
        for index in range(len(command) - width + 1)
    )


def command_set_values(command: list[str], name: str) -> list[str]:
    return [
        command[index + 2]
        for index in range(len(command) - 2)
        if command[index] == "+set" and command[index + 1] == name
    ]


def navigation_command_contract_errors(command: list[str]) -> list[str]:
    errors: list[str] = []
    required_values = (
        ("win_headless", "1"),
        ("in_enable", "0"),
        ("in_grab", "0"),
        ("r_fullscreen", "0"),
    )
    for name, expected in required_values:
        values = command_set_values(command, name)
        if values != [expected]:
            errors.append(
                f"navigation command requires exactly one {name}={expected}; "
                f"observed values={values!r}"
            )
    if "+ui_rml_runtime_synthetic_input" in command:
        errors.append("navigation command must not invoke runtime synthetic input")
    screenshot_commands = [
        token for token in command if token.lower().startswith("+screenshot")
    ]
    if screenshot_commands:
        errors.append(
            "navigation command must remain log-only and not request screenshots; "
            f"observed commands={screenshot_commands!r}"
        )
    return errors


def navigation_identifier_errors(
    base_game: str,
    evidence_id: str,
    *,
    lane_evidence_id: bool = False,
) -> list[str]:
    errors: list[str] = []
    if not NAVIGATION_BASE_GAME_PATTERN.fullmatch(base_game):
        errors.append(
            "navigation base game must be a simple portable identifier "
            "containing only letters, digits, dot, underscore, or hyphen"
        )
    evidence_pattern = (
        NAVIGATION_EVIDENCE_ID_PATTERN
        if lane_evidence_id
        else NAVIGATION_EVIDENCE_BASE_PATTERN
    )
    if not evidence_pattern.fullmatch(evidence_id):
        errors.append(
            "navigation evidence id must be a simple portable identifier "
            "containing only letters, digits, dot, underscore, or hyphen"
        )
    return errors


def path_is_link_or_reparse(path: Path) -> bool:
    try:
        metadata = path.lstat()
    except FileNotFoundError:
        return False
    return stat.S_ISLNK(metadata.st_mode) or bool(
        getattr(metadata, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    )


def runtime_home_redirect_errors(
    repo_root: Path,
    target_path: Path,
) -> list[str]:
    errors: list[str] = []
    lexical_repo_root = repo_root.absolute()
    lexical_target_path = target_path.absolute()
    try:
        relative_target = lexical_target_path.relative_to(lexical_repo_root)
    except ValueError:
        return ["navigation runtime path is not lexically below the repository root"]

    current = lexical_repo_root
    components = (current,)
    for component in relative_target.parts:
        current = current / component
        components += (current,)
    for component in components:
        try:
            redirected = path_is_link_or_reparse(component)
        except OSError as exc:
            errors.append(
                f"could not inspect navigation runtime path component {component}: {exc}"
            )
            continue
        if redirected:
            errors.append(
                "navigation runtime path component must not be a link or reparse point: "
                f"{component}"
            )
    return errors


def resolve_navigation_path(
    path: Path,
    description: str,
    errors: list[str],
) -> Path | None:
    try:
        return path.resolve()
    except (OSError, RuntimeError) as exc:
        errors.append(f"could not resolve {description}: {exc}")
        return None


def navigation_report_contract_errors(report: NavigationJourneyReport) -> list[str]:
    errors = navigation_command_contract_errors(report.command)
    errors.extend(
        navigation_identifier_errors(
            report.base_game,
            report.evidence_id,
            lane_evidence_id=True,
        )
    )
    report_cvars = (
        ("basedir", str(report.install_dir)),
        ("homedir", str(report.runtime_home)),
        ("game", report.base_game),
        ("r_renderer", report.renderer),
        ("s_enable", "0"),
        ("ui_rml_enable", "1"),
    )
    for name, expected in report_cvars:
        values = command_set_values(report.command, name)
        if values != [expected]:
            errors.append(
                f"navigation command requires exactly one {name}={expected}; "
                f"observed values={values!r}"
            )
    lexical_runtime_root = report.repo_root / DEFAULT_NAVIGATION_RUNTIME_DIR
    lexical_runtime_home = lexical_runtime_root / report.evidence_id
    lexical_log_path = (
        lexical_runtime_home
        / report.base_game
        / "logs"
        / f"{report.evidence_id}.log"
    )
    if report.runtime_home.absolute() != lexical_runtime_home.absolute():
        errors.append(
            "navigation runtime home does not match the confined lane-specific path"
        )
    if report.log_path.absolute() != lexical_log_path.absolute():
        errors.append(
            "navigation log path does not match the confined isolated-runtime evidence path"
        )

    # Inspect every existing lexical component before resolving anything.  This
    # makes junctions, symlinks, and reparse loops fail closed instead of being
    # followed by Path.resolve().
    redirect_errors = runtime_home_redirect_errors(report.repo_root, lexical_log_path)
    errors.extend(redirect_errors)
    if redirect_errors:
        return errors

    resolved_repo_root = resolve_navigation_path(
        report.repo_root, "navigation repository root", errors
    )
    runtime_root = resolve_navigation_path(
        lexical_runtime_root, "navigation runtime scratch root", errors
    )
    expected_runtime_home = resolve_navigation_path(
        lexical_runtime_home, "navigation lane runtime home", errors
    )
    expected_log_path = resolve_navigation_path(
        lexical_log_path, "navigation lane log path", errors
    )
    resolved_report_runtime_home = resolve_navigation_path(
        report.runtime_home, "reported navigation runtime home", errors
    )
    resolved_report_log_path = resolve_navigation_path(
        report.log_path, "reported navigation log path", errors
    )

    if resolved_repo_root is not None and runtime_root is not None:
        try:
            runtime_root.relative_to(resolved_repo_root)
        except ValueError:
            errors.append("navigation runtime scratch root resolves outside the repository")
    if runtime_root is not None and expected_runtime_home is not None:
        try:
            expected_runtime_home.relative_to(runtime_root)
        except ValueError:
            errors.append("navigation runtime home escapes the repository scratch root")
    if expected_runtime_home is not None and expected_log_path is not None:
        try:
            expected_log_path.relative_to(expected_runtime_home)
        except ValueError:
            errors.append("navigation log path escapes the lane-specific runtime home")
    if (
        expected_runtime_home is not None
        and resolved_report_runtime_home is not None
        and resolved_report_runtime_home != expected_runtime_home
    ):
        errors.append("navigation runtime home resolves away from its confined lane path")
    if (
        expected_log_path is not None
        and resolved_report_log_path is not None
        and resolved_report_log_path != expected_log_path
    ):
        errors.append(
            "navigation log path resolves away from its confined isolated-runtime path"
        )
    return errors


def validate_navigation_evidence(
    report: NavigationJourneyReport,
    *,
    min_mtime: float | None = None,
) -> None:
    base_dir = report.install_dir / report.base_game
    log_text = read_text_if_exists(report.log_path)
    expected_order = [name for name, _active, _route in NAVIGATION_CHECKPOINTS]
    observed_order, checkpoints = parse_navigation_checkpoints(log_text)
    route_documents = {
        route_id: base_dir / "ui" / "rml" / ROUTE_DOCUMENTS[route_id]
        for route_id in ("main", "options", "video")
    }
    report.checkpoints = checkpoints
    report.facts.update(
        {
            "journey_id": NAVIGATION_JOURNEY_ID,
            "renderer": report.renderer,
            "renderer_family": RENDERER_FAMILIES[report.renderer],
            "engine_exe_exists": report.engine_exe.is_file(),
            "install_dir_exists": report.install_dir.is_dir(),
            "base_game_dir_exists": base_dir.is_dir(),
            "journey_documents_exist": all(path.is_file() for path in route_documents.values()),
            "journey_documents": {
                route_id: {
                    "path": display_path(path, report.repo_root),
                    "exists": path.is_file(),
                }
                for route_id, path in route_documents.items()
            },
            "log_exists": report.log_path.is_file(),
            "expected_checkpoint_order": expected_order,
            "observed_checkpoint_order": observed_order,
            "checkpoint_order_ok": observed_order == expected_order,
            "synthetic_input_command_absent": "+ui_rml_runtime_synthetic_input"
            not in report.command,
            "synthetic_input_marker_absent": SYNTHETIC_INPUT_MARKER not in log_text,
            "final_inactive": bool(checkpoints)
            and checkpoints[-1].get("active") is False
            and checkpoints[-1].get("route") == "<none>",
        }
    )
    if min_mtime is not None and report.log_path.is_file():
        report.facts["log_fresh"] = report.log_path.stat().st_mtime >= min_mtime - 1
    elif min_mtime is not None:
        report.facts["log_fresh"] = False

    report.errors.extend(navigation_report_contract_errors(report))
    required = (
        ("engine_exe_exists", f"engine executable missing: {report.engine_exe}"),
        ("install_dir_exists", f"install directory missing: {report.install_dir}"),
        ("base_game_dir_exists", f"base game directory missing: {base_dir}"),
        ("journey_documents_exist", "one or more navigation journey documents are missing"),
        ("log_exists", f"runtime navigation log missing: {report.log_path}"),
        ("checkpoint_order_ok", "runtime navigation checkpoints were missing, duplicated, or out of order"),
        ("synthetic_input_command_absent", "runtime navigation command contains synthetic input"),
        ("synthetic_input_marker_absent", "runtime navigation log contains a synthetic input marker"),
        ("final_inactive", "runtime navigation did not finish inactive on route '<none>'"),
    )
    for fact, message in required:
        if not report.facts.get(fact):
            report.errors.append(message)

    expected_family = RENDERER_FAMILIES[report.renderer]
    input_fields = (
        "input_keys",
        "input_chars",
        "input_mouse_moves",
        "input_mouse_buttons",
        "input_mouse_wheels",
    )
    for checkpoint in checkpoints:
        name = checkpoint["name"]
        if checkpoint["marker_count"] != 1:
            report.errors.append(
                f"navigation checkpoint '{name}' marker count was {checkpoint['marker_count']}, expected 1"
            )
        for count_field, label in (
            ("status_count", "status"),
            ("frames_count", "frame counters"),
            ("route_counters_count", "route counters"),
            ("input_count", "input counters"),
        ):
            if checkpoint[count_field] != 1:
                report.errors.append(
                    f"navigation checkpoint '{name}' {label} count was "
                    f"{checkpoint[count_field]}, expected 1"
                )
        if checkpoint["active"] is not checkpoint["expected_active"]:
            report.errors.append(
                f"navigation checkpoint '{name}' active state was {checkpoint['active']!r}, "
                f"expected {checkpoint['expected_active']!r}"
            )
        if checkpoint["route"] != checkpoint["expected_route"]:
            report.errors.append(
                f"navigation checkpoint '{name}' route was {checkpoint['route']!r}, "
                f"expected {checkpoint['expected_route']!r}"
            )
        if checkpoint["renderer_family"] != expected_family:
            report.errors.append(
                f"navigation checkpoint '{name}' renderer family was "
                f"{checkpoint['renderer_family']!r}, expected {expected_family!r}"
            )
        if checkpoint["availability"] != "ready":
            report.errors.append(
                f"navigation checkpoint '{name}' runtime availability was "
                f"{checkpoint['availability']!r}, expected 'ready'"
            )
        if checkpoint["expected_active"] and (
            not isinstance(checkpoint["frame_updates"], int)
            or checkpoint["frame_updates"] <= 0
            or not isinstance(checkpoint["frame_renders"], int)
            or checkpoint["frame_renders"] <= 0
        ):
            report.errors.append(
                f"navigation checkpoint '{name}' did not record an update and render"
            )
        if checkpoint["route_synthetic_inputs"] != 0:
            report.errors.append(
                f"navigation checkpoint '{name}' synthetic input counter was "
                f"{checkpoint['route_synthetic_inputs']!r}, expected 0"
            )
        for input_field in input_fields:
            if checkpoint[input_field] != 0:
                report.errors.append(
                    f"navigation checkpoint '{name}' {input_field} was "
                    f"{checkpoint[input_field]!r}, expected 0"
                )

    if checkpoints:
        final = checkpoints[-1]
        if not isinstance(final["route_closes"], int) or final["route_closes"] <= 0:
            report.errors.append("final inactive checkpoint did not record a route close")
        if (
            not isinstance(final["route_close_requests"], int)
            or final["route_close_requests"] <= 0
        ):
            report.errors.append("final inactive checkpoint did not record a close request")

    if min_mtime is not None and not report.facts.get("log_fresh"):
        report.errors.append("runtime navigation log was not freshly written by this run")


def collect_navigation_evidence(report: NavigationJourneyReport) -> None:
    report.evidence_dir.mkdir(parents=True, exist_ok=True)
    target = report.evidence_dir / report.log_path.name
    if report.log_path.resolve() != target.resolve():
        shutil.copy2(report.log_path, target)
    report.copied_evidence["log"] = display_path(target, report.repo_root)


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


def write_navigation_matrix_manifest(
    reports: list[NavigationJourneyReport],
    manifest_path: Path,
    repo_root: Path,
) -> None:
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(
            navigation_matrix_report_payload(reports, repo_root, manifest_path),
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


def navigation_report_payload(report: NavigationJourneyReport) -> dict[str, Any]:
    return {
        "ok": report.ok,
        "journey": {
            "id": NAVIGATION_JOURNEY_ID,
            "expected_checkpoints": [
                {
                    "name": name,
                    "active": active,
                    "route": route,
                }
                for name, active, route in NAVIGATION_CHECKPOINTS
            ],
        },
        "renderer": report.renderer,
        "renderer_family": RENDERER_FAMILIES[report.renderer],
        "paths": {
            "repo_root": str(report.repo_root),
            "install_dir": display_path(report.install_dir, report.repo_root),
            "engine_exe": display_path(report.engine_exe, report.repo_root),
            "runtime_home": display_path(report.runtime_home, report.repo_root),
            "log": display_path(report.log_path, report.repo_root),
            "evidence_dir": display_path(report.evidence_dir, report.repo_root),
            "manifest": display_path(report.manifest_path, report.repo_root),
        },
        "command": report.command,
        "ran_engine": report.ran_engine,
        "exit_code": report.exit_code,
        "elapsed_seconds": report.elapsed_seconds,
        "facts": report.facts,
        "checkpoints": report.checkpoints,
        "copied_evidence": report.copied_evidence,
        "errors": report.errors,
    }


def navigation_matrix_report_payload(
    reports: list[NavigationJourneyReport],
    repo_root: Path,
    manifest_path: Path,
) -> dict[str, Any]:
    errors = [
        {
            "renderer": report.renderer,
            "evidence_id": report.evidence_id,
            "error": error,
        }
        for report in reports
        for error in report.errors
    ]
    return {
        "schema": NAVIGATION_EVIDENCE_SCHEMA,
        "ok": all(report.ok for report in reports),
        "journey_id": NAVIGATION_JOURNEY_ID,
        "paths": {
            "repo_root": str(repo_root),
            "manifest": display_path(manifest_path, repo_root),
        },
        "counts": {
            "renderer_lanes": len(reports),
            "passed": sum(1 for report in reports if report.ok),
            "failed": sum(1 for report in reports if not report.ok),
            "checkpoints_per_lane": len(NAVIGATION_CHECKPOINTS),
            "errors": len(errors),
        },
        "lanes": [navigation_report_payload(report) for report in reports],
        "errors": errors,
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
    print(
        f"  Guarded {report.renderer} status: "
        f"{yes_no(report.facts.get('guarded_renderer_status_seen'))}"
    )
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


def print_navigation_matrix_report(reports: list[NavigationJourneyReport]) -> None:
    print("RmlUi runtime route-stack navigation matrix:")
    for report in reports:
        result = "passed" if report.ok else "failed"
        observed = report.facts.get("observed_checkpoint_order") or []
        print(
            f"  {RENDERER_LABELS[report.renderer]}: {result}; "
            f"family={RENDERER_FAMILIES[report.renderer]}; "
            f"checkpoints={len(observed)}/{len(NAVIGATION_CHECKPOINTS)}"
        )
        for error in report.errors:
            print(f"    - {error}")
    if all(report.ok for report in reports):
        print("\nResult: RmlUi runtime route-stack navigation matrix passed.")
    else:
        print("\nResult: RmlUi runtime route-stack navigation matrix failed.")


def build_report(
    repo_root: Path,
    install_dir: Path,
    engine_exe: Path,
    base_game: str,
    renderer: str,
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
    server_address: str | None = None,
    seed_cvars: tuple[tuple[str, str], ...] = (),
) -> RuntimeCaptureReport:
    command = build_engine_command(
        engine_exe,
        install_dir,
        base_game,
        renderer,
        evidence_id,
        screenshot_format,
        route_id,
        geometry,
        startup_wait,
        capture_wait,
        status_wait,
        server_address,
        seed_cvars,
    )
    base_dir = install_dir / base_game
    return RuntimeCaptureReport(
        repo_root=repo_root,
        install_dir=install_dir,
        engine_exe=engine_exe,
        base_game=base_game,
        renderer=renderer,
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


def build_navigation_reports(
    repo_root: Path,
    install_dir: Path,
    engine_exe: Path,
    base_game: str,
    evidence_dir: Path,
    evidence_id: str,
    geometry: str,
    startup_wait: int,
    status_wait: int,
    manifest_path: Path,
) -> list[NavigationJourneyReport]:
    reports: list[NavigationJourneyReport] = []
    runtime_root = repo_root / DEFAULT_NAVIGATION_RUNTIME_DIR
    for renderer in NAVIGATION_RENDERERS:
        lane_evidence_id = f"{evidence_id}_navigation_{renderer}"
        runtime_home = runtime_root / lane_evidence_id
        reports.append(
            NavigationJourneyReport(
                repo_root=repo_root,
                install_dir=install_dir,
                engine_exe=engine_exe,
                base_game=base_game,
                renderer=renderer,
                evidence_dir=evidence_dir,
                runtime_home=runtime_home,
                evidence_id=lane_evidence_id,
                geometry=geometry,
                command=build_navigation_engine_command(
                    engine_exe,
                    install_dir,
                    runtime_home,
                    base_game,
                    renderer,
                    lane_evidence_id,
                    geometry,
                    startup_wait,
                    status_wait,
                ),
                log_path=(
                    runtime_home
                    / base_game
                    / "logs"
                    / f"{lane_evidence_id}.log"
                ),
                manifest_path=manifest_path,
            )
        )
    return reports


def run_engine_for_report(report: RuntimeCaptureReport, timeout: float) -> float | None:
    if not report.engine_exe.is_file():
        report.errors.append(f"engine executable missing: {report.engine_exe}")
        return None

    if report.renderer in {"vulkan", "rtx"}:
        report.screenshot_path.unlink(missing_ok=True)

    start_time = time.time()
    try:
        completed = subprocess.run(
            report.command,
            cwd=report.engine_exe.parent,
            env=build_run_environment(report.engine_exe),
            stdin=subprocess.DEVNULL,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        report.ran_engine = True
        report.elapsed_seconds = time.time() - start_time
        report.errors.append(f"engine timed out after {timeout:g} seconds")
        return start_time

    report.ran_engine = True
    report.exit_code = completed.returncode
    report.elapsed_seconds = time.time() - start_time
    if completed.returncode != 0:
        report.errors.append(f"engine exited with code {completed.returncode}")

    return start_time


def run_engine_for_navigation_report(
    report: NavigationJourneyReport,
    timeout: float,
) -> float | None:
    if os.name != "nt":
        report.errors.append(
            "live runtime navigation requires the verified Windows win_headless hidden-window path"
        )
        return None
    for error in navigation_report_contract_errors(report):
        if error not in report.errors:
            report.errors.append(error)
    if report.errors:
        return None
    if not report.engine_exe.is_file():
        report.errors.append(f"engine executable missing: {report.engine_exe}")
        return None
    try:
        report.runtime_home.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        report.errors.append(f"could not create navigation runtime parent path: {exc}")
        return None
    for error in navigation_report_contract_errors(report):
        if error not in report.errors:
            report.errors.append(error)
    if report.errors:
        return None

    # Atomically claim the lane leaf.  Refusing every pre-existing home keeps
    # concurrent same-evidence runs and archived state from sharing a homedir.
    try:
        report.runtime_home.mkdir(exist_ok=False)
    except FileExistsError:
        report.errors.append(
            "isolated navigation runtime home must not already exist; "
            "atomic lane claim refused"
        )
        return None
    except OSError as exc:
        report.errors.append(f"could not atomically claim navigation runtime home: {exc}")
        return None

    for error in navigation_report_contract_errors(report):
        if error not in report.errors:
            report.errors.append(error)
    if report.errors:
        return None

    # The new leaf should still be empty immediately before launch.  This
    # catches accidental state introduced by harness-side setup after claiming.
    try:
        with os.scandir(report.runtime_home) as entries:
            runtime_home_has_entries = next(entries, None) is not None
    except OSError as exc:
        report.errors.append(f"could not inspect isolated navigation runtime home: {exc}")
        return None
    if runtime_home_has_entries:
        report.errors.append(
            "atomically claimed navigation runtime home was modified before launch; "
            "refusing config or log state"
        )
        return None

    # Re-run the complete lexical-component, reparse, resolve, and containment
    # contract immediately before process creation.
    for error in navigation_report_contract_errors(report):
        if error not in report.errors:
            report.errors.append(error)
    if report.errors:
        return None

    start_time = time.time()
    try:
        completed = subprocess.run(
            report.command,
            cwd=report.engine_exe.parent,
            env=build_run_environment(report.engine_exe),
            stdin=subprocess.DEVNULL,
            timeout=timeout,
            check=False,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
    except subprocess.TimeoutExpired:
        report.ran_engine = True
        report.elapsed_seconds = time.time() - start_time
        report.errors.append(f"engine timed out after {timeout:g} seconds")
        return start_time

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
    renderer: str,
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
            renderer,
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
    renderer: str,
    evidence_dir: Path,
    evidence_id: str,
    screenshot_format: str,
    startup_wait: int,
    capture_wait: int,
    status_wait: int,
    route_ids: tuple[str, ...] = DEFAULT_ROUTE_MATRIX,
) -> list[RuntimeCaptureReport]:
    return [
        build_report(
            repo_root,
            install_dir,
            engine_exe,
            base_game,
            renderer,
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
            None,
            ROUTE_SEED_CVARS.get(route_id, ()),
        )
        for route_id in route_ids
    ]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--install-dir", type=Path, default=DEFAULT_INSTALL_DIR)
    parser.add_argument(
        "--engine-exe",
        type=Path,
        default=None,
        help="Engine executable; defaults to worr_x86_64.exe inside --install-dir.",
    )
    parser.add_argument("--base-game", default=DEFAULT_BASE_GAME)
    parser.add_argument(
        "--renderer",
        choices=tuple(RENDERER_FAMILIES),
        default=DEFAULT_RENDERER,
        help="Native renderer lane used for the capture.",
    )
    parser.add_argument("--evidence-dir", type=Path, default=DEFAULT_EVIDENCE_DIR)
    parser.add_argument("--evidence-id", default=DEFAULT_EVIDENCE_ID)
    parser.add_argument("--screenshot-format", choices=("tga", "png"), default=DEFAULT_SCREENSHOT_FORMAT)
    parser.add_argument("--route-id", default=DEFAULT_ROUTE_ID, choices=tuple(ROUTE_DOCUMENTS))
    parser.add_argument(
        "--server-address",
        default=None,
        help="Seed adr0 for a guarded servers-route capture (for example 127.0.0.1:27910).",
    )
    parser.add_argument(
        "--seed-cvar",
        action="append",
        type=parse_seed_cvar,
        default=[],
        metavar="NAME=VALUE",
        help="Seed a cvar immediately before opening a single guarded route; repeat as needed.",
    )
    parser.add_argument("--geometry", default=None, help="Windowed capture geometry as WIDTHxHEIGHT.")
    parser.add_argument("--matrix", action="store_true", help="Run or validate the default viewport matrix.")
    parser.add_argument("--route-matrix", action="store_true", help="Run or validate the guarded menu route matrix.")
    parser.add_argument(
        "--full-route-matrix",
        action="store_true",
        help="Run or validate every end-user route in the migration manifest.",
    )
    parser.add_argument(
        "--renderer-matrix",
        action="store_true",
        help="Run or validate the guarded OpenGL route matrix plus renderer-family guardrails.",
    )
    parser.add_argument(
        "--navigation-matrix",
        action="store_true",
        help=(
            "Run or validate the input-free main/options/video route-stack journey "
            "through all native renderer families."
        ),
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
    engine_exe_arg = (
        args.engine_exe
        if args.engine_exe is not None
        else args.install_dir / DEFAULT_ENGINE_EXE.name
    )
    engine_exe = resolve_path(engine_exe_arg, repo_root)
    evidence_dir = resolve_path(args.evidence_dir, repo_root)
    manifest_path = resolve_path(args.write_manifest, repo_root) if args.write_manifest else None

    if args.geometry:
        try:
            parse_geometry_dimensions(args.geometry)
        except ValueError as exc:
            parser.error(str(exc))

    if args.server_address and args.route_id != "servers":
        parser.error("--server-address is only valid with --route-id servers")

    matrix_modes = [
        args.matrix,
        args.route_matrix,
        args.full_route_matrix,
        args.renderer_matrix,
        args.navigation_matrix,
    ]
    if sum(1 for enabled in matrix_modes if enabled) > 1:
        parser.error(
            "--matrix, --route-matrix, --full-route-matrix, and "
            "--renderer-matrix or --navigation-matrix cannot be combined"
        )
    if args.seed_cvar and any(matrix_modes):
        parser.error("--seed-cvar is only valid for a single route capture")
    if args.navigation_matrix:
        identifier_errors = navigation_identifier_errors(
            args.base_game,
            args.evidence_id,
        )
        if identifier_errors:
            parser.error(identifier_errors[0])
        if args.startup_wait < 0:
            parser.error("--startup-wait must be non-negative for navigation evidence")
        if args.status_wait <= 0:
            parser.error("--status-wait must be positive for navigation evidence")
        if (
            not math.isfinite(args.timeout)
            or args.timeout <= 0
            or args.timeout > MAX_NAVIGATION_TIMEOUT
        ):
            parser.error(
                "--timeout must be finite, positive, and no greater than "
                f"{MAX_NAVIGATION_TIMEOUT:g} seconds for navigation evidence"
            )

    if args.navigation_matrix:
        navigation_manifest_path = manifest_path or evidence_dir / "navigation-matrix.json"
        reports = build_navigation_reports(
            repo_root,
            install_dir,
            engine_exe,
            args.base_game,
            evidence_dir,
            args.evidence_id,
            args.geometry or DEFAULT_ROUTE_MATRIX_GEOMETRY,
            args.startup_wait,
            args.status_wait,
            navigation_manifest_path,
        )

        if args.dry_run:
            if args.format == "json":
                print(
                    json.dumps(
                        navigation_matrix_report_payload(
                            reports,
                            repo_root,
                            navigation_manifest_path,
                        ),
                        indent=2,
                        sort_keys=True,
                    )
                )
            else:
                for report in reports:
                    print(" ".join(report.command))
            return 0

        for index, report in enumerate(reports, start=1):
            start_time = (
                run_engine_for_navigation_report(report, args.timeout)
                if args.run
                else None
            )
            validate_navigation_evidence(
                report,
                min_mtime=start_time if args.run else None,
            )
            if report.ok:
                collect_navigation_evidence(report)
            if args.run and args.format != "json":
                state = "passed" if report.ok else "failed"
                print(
                    f"  [{index}/{len(reports)}] {report.renderer}: {state}",
                    flush=True,
                )

        write_navigation_matrix_manifest(reports, navigation_manifest_path, repo_root)
        if args.format == "json":
            print(
                json.dumps(
                    navigation_matrix_report_payload(
                        reports,
                        repo_root,
                        navigation_manifest_path,
                    ),
                    indent=2,
                    sort_keys=True,
                )
            )
        else:
            print_navigation_matrix_report(reports)
        return 0 if all(report.ok for report in reports) else 1

    if args.matrix:
        reports = build_matrix_reports(
            repo_root,
            install_dir,
            engine_exe,
            args.base_game,
            args.renderer,
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

        for index, report in enumerate(reports, start=1):
            start_time = run_engine_for_report(report, args.timeout) if args.run else None
            validate_evidence(report, min_mtime=start_time if args.run else None)
            if report.ok:
                collect_evidence(report)
            if args.run and args.format != "json":
                state = "passed" if report.ok else "failed"
                print(
                    f"  [{index}/{len(reports)}] {report.route_id}: {state}",
                    flush=True,
                )

        write_matrix_manifest(reports, manifest_path, repo_root)

        if args.format == "json":
            print(json.dumps(matrix_report_payload(reports, repo_root, manifest_path), indent=2, sort_keys=True))
        else:
            print_matrix_report(reports)

        return 0 if all(report.ok for report in reports) else 1

    if args.route_matrix or args.full_route_matrix:
        reports = build_route_reports(
            repo_root,
            install_dir,
            engine_exe,
            args.base_game,
            args.renderer,
            evidence_dir,
            args.evidence_id,
            args.screenshot_format,
            args.startup_wait,
            args.capture_wait,
            args.status_wait,
            FULL_ROUTE_MATRIX if args.full_route_matrix else DEFAULT_ROUTE_MATRIX,
        )

        if args.dry_run:
            if args.format == "json":
                print(json.dumps(route_matrix_report_payload(reports, repo_root, manifest_path), indent=2, sort_keys=True))
            else:
                for report in reports:
                    print(" ".join(report.command))
            return 0

        for index, report in enumerate(reports, start=1):
            start_time = run_engine_for_report(report, args.timeout) if args.run else None
            validate_evidence(report, min_mtime=start_time if args.run else None)
            if report.ok:
                collect_evidence(report)
            if args.run and args.format != "json":
                state = "passed" if report.ok else "failed"
                print(
                    f"  [{index}/{len(reports)}] {report.route_id}: {state}",
                    flush=True,
                )

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
            args.renderer,
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
                    "Vulkan=native_guarded, "
                    "RTX/vkpt=native_guarded"
                )
                print(
                    "# Bridge readiness lanes: "
                    "Vulkan=native_guarded, "
                    "RTX/vkpt=native_guarded"
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
        args.renderer,
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
        args.server_address,
        tuple(args.seed_cvar),
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
