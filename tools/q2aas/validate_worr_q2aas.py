#!/usr/bin/env python3
"""Run WORR q2aas config and map-conversion smoke checks."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path, PurePosixPath

Q2_BSP_IDENT = b"IBSP"
Q2_BSP_VERSION = 38
Q2_HEADER_LUMPS = 19
Q2_HEADER_SIZE = 8 + Q2_HEADER_LUMPS * 8
VALIDATION_MANIFEST_SCHEMA = "worr-q2aas-validation-manifest-v1"
VALIDATION_MANIFEST_VERSION = 1
Q2_LUMP_NAMES = (
    "entities",
    "planes",
    "vertexes",
    "visibility",
    "nodes",
    "texinfo",
    "faces",
    "lighting",
    "leafs",
    "leaffaces",
    "leafbrushes",
    "edges",
    "surfedges",
    "models",
    "brushes",
    "brushsides",
    "pop",
    "areas",
    "areaportals",
)

Q2_CONTENT_FLAGS = {
    "solid": 0x00000001,
    "window": 0x00000002,
    "aux": 0x00000004,
    "lava": 0x00000008,
    "slime": 0x00000010,
    "water": 0x00000020,
    "mist": 0x00000040,
    "areaportal": 0x00008000,
    "playerclip": 0x00010000,
    "monsterclip": 0x00020000,
    "current_0": 0x00040000,
    "current_90": 0x00080000,
    "current_180": 0x00100000,
    "current_270": 0x00200000,
    "current_up": 0x00400000,
    "current_down": 0x00800000,
    "origin": 0x01000000,
    "monster": 0x02000000,
    "deadmonster": 0x04000000,
    "detail": 0x08000000,
    "translucent": 0x10000000,
    "ladder": 0x20000000,
}

ENTITY_CLASS_GROUPS = {
    "spawn_points": (
        "info_player_start",
        "info_player_deathmatch",
        "info_player_coop",
    ),
    "intermissions": ("info_player_intermission",),
    "items": ("item_", "weapon_", "ammo_", "key_"),
    "high_value_items": (
        "item_quad",
        "item_invulnerability",
        "item_power_screen",
        "item_power_shield",
        "weapon_bfg",
        "weapon_railgun",
        "weapon_rocketlauncher",
    ),
    "movers": (
        "func_door",
        "func_door_rotating",
        "func_plat",
        "func_train",
        "func_button",
        "func_rotating",
        "func_bobbing",
        "func_water",
        "func_conveyor",
    ),
    "doors": ("func_door", "func_door_rotating"),
    "elevators": ("func_plat", "func_train"),
    "teleports": ("trigger_teleport", "misc_teleporter_dest"),
    "triggers": ("trigger_",),
    "hurt": ("trigger_hurt",),
}

AAS_IDENT = 0x53414145
AAS_VERSION = 5
AAS_VERSION_OLD = 4
AAS_LUMPS = 14
AAS_HEADER_SIZE = 12 + AAS_LUMPS * 8
AAS_LUMP_NAMES = (
    "bboxes",
    "vertexes",
    "planes",
    "edges",
    "edgeindex",
    "faces",
    "faceindex",
    "areas",
    "areasettings",
    "reachability",
    "nodes",
    "portals",
    "portalindex",
    "clusters",
)
AAS_AREA_STRUCT = struct.Struct("<iii9f")
AAS_AREA_SETTINGS_STRUCT = struct.Struct("<iiiiiii")
AAS_REACHABILITY_STRUCT = struct.Struct("<iii6fiH2x")

INTERESTING_PREFIXES = (
    "bsp2aas:",
    "-- Q2_LoadMapFromBSP --",
    "AAS created",
    "writing ",
)

INTERESTING_METRICS = (
    "numvertexes",
    "numplanes",
    "numedges",
    "numfaces",
    "numareas",
    "numareasettings",
    "reachabilitysize",
    "numclusters",
)

TRAVEL_NAMES = (
    "walk",
    "crouch",
    "barrier jump",
    "jump",
    "ladder",
    "walk off ledge",
    "swim",
    "water jump",
    "teleport",
    "elevator",
    "rocket jump",
    "bfg jump",
    "grapple hook",
    "double jump",
    "ramp jump",
    "strafe jump",
    "jump pad",
    "func bob",
)

VALID_MANIFEST_KEYS = {
    "schema",
    "version",
    "task_ids",
    "maps",
    "pending_reference_maps",
    "reference_coverage",
}

VALID_MANIFEST_MAP_KEYS = {
    "id",
    "path",
    "archive",
    "archive_member",
    "required",
    "require_reachability",
    "require_clean_bsp_lumps",
    "require_spawn_coverage",
    "require_item_coverage",
    "require_high_value_reachability",
    "coverage_categories",
    "minimum_metrics",
    "minimum_travel_counts",
    "notes",
}

VALID_REFERENCE_COVERAGE_KEYS = {
    "id",
    "description",
    "map_ids",
    "minimum_validated_maps",
    "notes",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_tool(root: Path, build_dir: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return root / build_dir / "tools" / "q2aas" / f"worr_q2aas{suffix}"


def run_command(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    printable = " ".join(command)
    print(f"[q2aas] {printable}")
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def extract_metric(output: str, name: str) -> int | None:
    matches = re.findall(rf"\b{re.escape(name)}\s*=\s*(\d+)", output)
    return int(matches[-1]) if matches else None


def extract_travel_counts(output: str) -> dict[str, int]:
    counts: dict[str, int] = {}
    for line in output.splitlines():
        stripped = line.strip()
        for name in TRAVEL_NAMES:
            match = re.fullmatch(rf"(\d+)\s+{re.escape(name)}", stripped)
            if match:
                counts[name] = int(match.group(1))
                break
    return counts


def extract_metrics(output: str) -> dict[str, int]:
    metrics: dict[str, int] = {}
    for name in INTERESTING_METRICS:
        value = extract_metric(output, name)
        if value is not None:
            metrics[name] = value
    return metrics


def extract_tool_version_line(output: str) -> str | None:
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("BSPC version "):
            return stripped
    return None


def pack_ident_text(ident: int) -> str:
    raw = struct.pack("<I", ident)
    return "".join(chr(value) if 32 <= value <= 126 else "." for value in raw)


def q2_lump_data(data: bytes, lumps: dict[str, dict[str, int]], name: str) -> bytes:
    lump = lumps.get(name)
    if not lump:
        return b""
    offset = int(lump["offset"])
    length = int(lump["length"])
    if offset < 0 or length <= 0 or offset + length > len(data):
        return b""
    return data[offset:offset + length]


def count_brush_contents(data: bytes, lumps: dict[str, dict[str, int]]) -> dict[str, object]:
    brush_data = q2_lump_data(data, lumps, "brushes")
    brush_size = 12
    brush_count = len(brush_data) // brush_size
    flag_counts = {name: 0 for name in Q2_CONTENT_FLAGS}
    raw_counts: dict[str, int] = {}

    for index in range(brush_count):
        _, _, contents = struct.unpack_from("<iii", brush_data, index * brush_size)
        raw_key = f"0x{contents & 0xFFFFFFFF:08x}"
        raw_counts[raw_key] = raw_counts.get(raw_key, 0) + 1
        for name, mask in Q2_CONTENT_FLAGS.items():
            if contents & mask:
                flag_counts[name] += 1

    return {
        "total_brushes": brush_count,
        "flag_counts": flag_counts,
        "raw_contents": dict(sorted(raw_counts.items())),
        "liquid_brushes": flag_counts["water"] + flag_counts["slime"] + flag_counts["lava"],
        "hazard_brushes": flag_counts["slime"] + flag_counts["lava"],
        "ladder_brushes": flag_counts["ladder"],
        "clip_brushes": flag_counts["playerclip"] + flag_counts["monsterclip"],
    }


def inspect_q2_bsp(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    info: dict[str, object] = {
        "size": len(data),
        "header_size": Q2_HEADER_SIZE,
        "valid_q2_bsp": False,
        "format": "unknown",
        "bspx_detected": False,
        "bspx_offsets": [],
        "lump_issues": [],
        "lumps": {},
    }

    if len(data) < 8:
        info["error"] = "file is too small to contain a BSP header"
        return info

    ident = data[:4]
    version = struct.unpack_from("<i", data, 4)[0]
    info.update({
        "ident": ident.decode("ascii", errors="replace"),
        "version": version,
    })

    if ident == Q2_BSP_IDENT and version == Q2_BSP_VERSION:
        info["valid_q2_bsp"] = True
        info["format"] = "quake2_ibsp38"
    elif ident == Q2_BSP_IDENT:
        info["format"] = "ibsp_unknown_version"

    if len(data) < Q2_HEADER_SIZE:
        info["error"] = "file is too small to contain the full Quake II BSP lump table"
        return info

    lumps: dict[str, dict[str, int]] = {}
    issues: list[str] = []
    standard_lump_end = Q2_HEADER_SIZE
    for index, name in enumerate(Q2_LUMP_NAMES):
        offset, length = struct.unpack_from("<ii", data, 8 + index * 8)
        lumps[name] = {"offset": offset, "length": length}
        if offset < 0 or length < 0:
            issues.append(f"{name} has negative offset/length")
            continue
        if offset + length > len(data):
            issues.append(f"{name} extends beyond end of file")
            continue
        standard_lump_end = max(standard_lump_end, offset + length)
    info["lumps"] = lumps
    info["lump_issues"] = issues
    info["standard_lump_end"] = standard_lump_end

    bspx_offsets = [
        match.start() + standard_lump_end
        for match in re.finditer(re.escape(b"BSPX"), data[standard_lump_end:])
    ]
    if bspx_offsets:
        info["bspx_detected"] = True
        info["bspx_offsets"] = bspx_offsets

    info["brush_contents"] = count_brush_contents(data, lumps)

    return info


def tokenize_entities(text: str) -> list[str]:
    tokens: list[str] = []
    index = 0
    length = len(text)

    while index < length:
        char = text[index]
        if char.isspace():
            index += 1
            continue
        if char in "{}":
            tokens.append(char)
            index += 1
            continue
        if char == '"':
            index += 1
            value: list[str] = []
            while index < length:
                char = text[index]
                if char == "\\" and index + 1 < length:
                    value.append(text[index + 1])
                    index += 2
                    continue
                if char == '"':
                    index += 1
                    break
                value.append(char)
                index += 1
            tokens.append("".join(value))
            continue

        start = index
        while index < length and not text[index].isspace() and text[index] not in "{}":
            index += 1
        tokens.append(text[start:index])

    return tokens


def parse_entity_lump(data: bytes, lumps: dict[str, dict[str, int]]) -> list[dict[str, str]]:
    text = q2_lump_data(data, lumps, "entities").decode("latin-1", errors="replace")
    tokens = tokenize_entities(text)
    entities: list[dict[str, str]] = []
    index = 0

    while index < len(tokens):
        if tokens[index] != "{":
            index += 1
            continue
        index += 1
        entity: dict[str, str] = {}
        while index < len(tokens) and tokens[index] != "}":
            if index + 1 >= len(tokens):
                break
            key = tokens[index]
            value = tokens[index + 1]
            if key in ("{", "}"):
                index += 1
                continue
            entity[key] = value
            index += 2
        if index < len(tokens) and tokens[index] == "}":
            index += 1
        if entity:
            entities.append(entity)

    return entities


def parse_origin(value: str | None) -> tuple[float, float, float] | None:
    if not value:
        return None
    parts = value.replace(",", " ").split()
    if len(parts) < 3:
        return None
    try:
        return (float(parts[0]), float(parts[1]), float(parts[2]))
    except ValueError:
        return None


def classname_matches(classname: str, patterns: tuple[str, ...]) -> bool:
    return any(classname == pattern or classname.startswith(pattern) for pattern in patterns)


def entity_summary(entity: dict[str, str], area: int | None = None, reason: str | None = None) -> dict[str, object]:
    summary: dict[str, object] = {
        "classname": entity.get("classname", ""),
    }
    if "origin" in entity:
        summary["origin"] = entity["origin"]
    if "targetname" in entity:
        summary["targetname"] = entity["targetname"]
    if "target" in entity:
        summary["target"] = entity["target"]
    if "model" in entity:
        summary["model"] = entity["model"]
    if area is not None:
        summary["area"] = area
    if reason:
        summary["reason"] = reason
    return summary


def group_entities(entities: list[dict[str, str]]) -> dict[str, list[dict[str, str]]]:
    groups: dict[str, list[dict[str, str]]] = {name: [] for name in ENTITY_CLASS_GROUPS}
    for entity in entities:
        classname = entity.get("classname", "")
        for group, patterns in ENTITY_CLASS_GROUPS.items():
            if classname_matches(classname, patterns):
                groups[group].append(entity)
    return groups


def summarize_entity_groups(groups: dict[str, list[dict[str, str]]]) -> dict[str, object]:
    summary: dict[str, object] = {}
    for group, entities in groups.items():
        by_classname: dict[str, int] = {}
        with_origin = 0
        for entity in entities:
            classname = entity.get("classname", "")
            by_classname[classname] = by_classname.get(classname, 0) + 1
            if parse_origin(entity.get("origin")) is not None:
                with_origin += 1
        summary[group] = {
            "total": len(entities),
            "with_origin": with_origin,
            "without_origin": len(entities) - with_origin,
            "by_classname": dict(sorted(by_classname.items())),
        }
    return summary


def decode_aas_header(path: Path) -> dict[str, object]:
    data = bytearray(path.read_bytes()[:AAS_HEADER_SIZE])
    info: dict[str, object] = {
        "header_size": AAS_HEADER_SIZE,
        "valid_aas": False,
        "lumps": {},
    }

    if len(data) < AAS_HEADER_SIZE:
        info["error"] = "file is too small to contain an AAS header"
        return info

    ident, version = struct.unpack_from("<ii", data, 0)
    if version == AAS_VERSION:
        for index in range(8, len(data)):
            data[index] ^= ((index - 8) * 119) & 0xFF
        ident, version = struct.unpack_from("<ii", data, 0)

    bspchecksum = struct.unpack_from("<i", data, 8)[0]
    info.update({
        "ident": pack_ident_text(ident),
        "version": version,
        "bspchecksum": bspchecksum,
        "bspchecksum_unsigned": bspchecksum & 0xFFFFFFFF,
        "valid_aas": ident == AAS_IDENT and version in (AAS_VERSION_OLD, AAS_VERSION),
    })

    lumps: dict[str, dict[str, int]] = {}
    for index, name in enumerate(AAS_LUMP_NAMES):
        offset, length = struct.unpack_from("<ii", data, 12 + index * 8)
        lumps[name] = {"offset": offset, "length": length}
    info["lumps"] = lumps
    return info


def read_aas_lump(path: Path, aas_header: dict[str, object], name: str) -> bytes:
    lumps = aas_header.get("lumps", {})
    if not isinstance(lumps, dict):
        return b""
    lump = lumps.get(name)
    if not isinstance(lump, dict):
        return b""
    offset = int(lump.get("offset", 0))
    length = int(lump.get("length", 0))
    if offset < 0 or length <= 0:
        return b""
    with path.open("rb") as handle:
        handle.seek(offset)
        return handle.read(length)


def read_aas_navigation(path: Path, aas_header: dict[str, object]) -> dict[str, object]:
    areas_data = read_aas_lump(path, aas_header, "areas")
    settings_data = read_aas_lump(path, aas_header, "areasettings")
    reachability_data = read_aas_lump(path, aas_header, "reachability")

    areas: list[dict[str, object]] = []
    for offset in range(0, len(areas_data) - AAS_AREA_STRUCT.size + 1, AAS_AREA_STRUCT.size):
        unpacked = AAS_AREA_STRUCT.unpack_from(areas_data, offset)
        areas.append({
            "areanum": unpacked[0],
            "mins": unpacked[3:6],
            "maxs": unpacked[6:9],
            "center": unpacked[9:12],
        })

    settings: list[dict[str, int]] = []
    for offset in range(0, len(settings_data) - AAS_AREA_SETTINGS_STRUCT.size + 1, AAS_AREA_SETTINGS_STRUCT.size):
        unpacked = AAS_AREA_SETTINGS_STRUCT.unpack_from(settings_data, offset)
        settings.append({
            "contents": unpacked[0],
            "areaflags": unpacked[1],
            "presencetype": unpacked[2],
            "cluster": unpacked[3],
            "clusterareanum": unpacked[4],
            "numreachableareas": unpacked[5],
            "firstreachablearea": unpacked[6],
        })

    reachability: list[int] = []
    for offset in range(0, len(reachability_data) - AAS_REACHABILITY_STRUCT.size + 1, AAS_REACHABILITY_STRUCT.size):
        unpacked = AAS_REACHABILITY_STRUCT.unpack_from(reachability_data, offset)
        reachability.append(unpacked[0])

    graph: dict[int, list[int]] = {}
    for index, setting in enumerate(settings):
        first = setting["firstreachablearea"]
        count = setting["numreachableareas"]
        graph[index] = [
            area
            for area in reachability[first:first + count]
            if area > 0
        ]

    return {
        "areas": areas,
        "settings": settings,
        "reachability": reachability,
        "graph": graph,
    }


def point_in_bounds(
    point: tuple[float, float, float],
    mins: tuple[float, float, float],
    maxs: tuple[float, float, float],
    tolerance: float,
) -> bool:
    return all(mins[index] - tolerance <= point[index] <= maxs[index] + tolerance for index in range(3))


def area_for_origin(
    origin: tuple[float, float, float],
    areas: list[dict[str, object]],
    tolerance: float = 32.0,
) -> int | None:
    best_area: int | None = None
    best_distance = float("inf")
    for area in areas:
        areanum = int(area["areanum"])
        if areanum <= 0:
            continue
        mins = tuple(float(value) for value in area["mins"])
        maxs = tuple(float(value) for value in area["maxs"])
        if not point_in_bounds(origin, mins, maxs, tolerance):
            continue
        center = tuple(float(value) for value in area["center"])
        distance = sum((origin[index] - center[index]) ** 2 for index in range(3))
        if distance < best_distance:
            best_distance = distance
            best_area = areanum
    return best_area


def reachable_areas_from(starts: set[int], graph: dict[int, list[int]]) -> set[int]:
    seen = {area for area in starts if area > 0}
    queue = list(seen)
    while queue:
        current = queue.pop(0)
        for target in graph.get(current, []):
            if target not in seen:
                seen.add(target)
                queue.append(target)
    return seen


def origin_coverage(
    entities: list[dict[str, str]],
    areas: list[dict[str, object]],
) -> tuple[dict[str, object], dict[int, int]]:
    area_counts: dict[int, int] = {}
    missing_origin: list[dict[str, object]] = []
    outside_areas: list[dict[str, object]] = []
    mapped = 0

    for entity in entities:
        origin = parse_origin(entity.get("origin"))
        if origin is None:
            missing_origin.append(entity_summary(entity, reason="missing_origin"))
            continue
        area = area_for_origin(origin, areas)
        if area is None:
            outside_areas.append(entity_summary(entity, reason="outside_aas_area_bounds"))
            continue
        area_counts[area] = area_counts.get(area, 0) + 1
        mapped += 1

    return {
        "total": len(entities),
        "mapped_to_aas_area": mapped,
        "missing_origin": len(missing_origin),
        "outside_aas_area_bounds": len(outside_areas),
        "missing_origin_entities": missing_origin[:32],
        "outside_aas_area_entities": outside_areas[:32],
        "areas": dict(sorted((str(area), count) for area, count in area_counts.items())),
    }, area_counts


def build_map_diagnostics(
    map_path: Path,
    bsp_header: dict[str, object],
    aas_path: Path,
    aas_header: dict[str, object],
) -> dict[str, object]:
    data = map_path.read_bytes()
    lumps = bsp_header.get("lumps", {})
    entities = parse_entity_lump(data, lumps if isinstance(lumps, dict) else {})
    groups = group_entities(entities)
    classname_counts: dict[str, int] = {}
    for entity in entities:
        classname = entity.get("classname", "")
        classname_counts[classname] = classname_counts.get(classname, 0) + 1

    navigation = read_aas_navigation(aas_path, aas_header)
    areas = navigation["areas"]
    graph = navigation["graph"]

    spawn_coverage, spawn_area_counts = origin_coverage(groups["spawn_points"], areas)
    item_coverage, _ = origin_coverage(groups["items"], areas)
    high_value_coverage, high_value_area_counts = origin_coverage(groups["high_value_items"], areas)
    reachable_from_spawns = reachable_areas_from(set(spawn_area_counts), graph)

    unreachable_high_value: list[dict[str, object]] = []
    for entity in groups["high_value_items"]:
        origin = parse_origin(entity.get("origin"))
        if origin is None:
            unreachable_high_value.append(entity_summary(entity, reason="missing_origin"))
            continue
        area = area_for_origin(origin, areas)
        if area is None:
            unreachable_high_value.append(entity_summary(entity, reason="outside_aas_area_bounds"))
            continue
        if area not in reachable_from_spawns:
            unreachable_high_value.append(entity_summary(entity, area=area, reason="not_reachable_from_any_spawn_area"))

    mover_report = {
        "doors": [entity_summary(entity) for entity in groups["doors"][:32]],
        "elevators": [entity_summary(entity) for entity in groups["elevators"][:32]],
        "teleports": [entity_summary(entity) for entity in groups["teleports"][:32]],
        "hurt_triggers": [entity_summary(entity) for entity in groups["hurt"][:32]],
    }

    return {
        "entities": {
            "total": len(entities),
            "classname_counts": dict(sorted(classname_counts.items())),
            "groups": summarize_entity_groups(groups),
        },
        "brush_contents": bsp_header.get("brush_contents", {}),
        "origin_coverage": {
            "spawn_points": spawn_coverage,
            "items": item_coverage,
            "high_value_items": high_value_coverage,
        },
        "reachability_from_spawns": {
            "spawn_area_count": len(spawn_area_counts),
            "reachable_area_count": len(reachable_from_spawns),
            "high_value_item_area_count": len(high_value_area_counts),
            "unreachable_high_value_items": len(unreachable_high_value),
            "unreachable_high_value_item_entities": unreachable_high_value[:32],
            "method": "AAS area bounding-box assignment plus generated reachability graph.",
        },
        "mover_entity_report": mover_report,
    }


def build_aas_metadata(
    tool: Path,
    cfg: Path,
    output: Path,
    command: list[str],
    map_id: object,
    map_path: Path,
    aas_path: Path,
    tool_sha256: str,
    cfg_sha256: str,
    map_sha256: str,
    aas_sha256: str,
    tool_version_line: str | None,
    bsp_header: dict[str, object],
    aas_header: dict[str, object],
    diagnostics: dict[str, object],
    map_source: dict[str, object],
) -> dict[str, object]:
    return {
        "schema": "worr-q2aas-metadata-v1",
        "map_id": map_id,
        "generator": {
            "tool": str(tool),
            "tool_sha256": tool_sha256,
            "tool_version": tool_version_line,
            "cfg": str(cfg),
            "cfg_sha256": cfg_sha256,
            "command": command,
        },
        "input": {
            "bsp": str(map_path),
            "map_source": map_source,
            "bsp_sha256": map_sha256,
            "bsp_header": bsp_header,
            "diagnostics": diagnostics,
        },
        "output": {
            "directory": str(output),
            "aas": str(aas_path),
            "aas_sha256": aas_sha256,
            "aas_header": aas_header,
        },
        "reproducibility": {
            "generation_time": None,
            "generation_time_policy": "Omitted by default. Recreate identity from tool/config/input/output hashes.",
            "sidecar_format": "deterministic_json_sorted_keys",
        },
    }


def write_metadata_sidecar(aas_path: Path, metadata: dict[str, object]) -> Path:
    sidecar_path = aas_path.with_suffix(aas_path.suffix + ".meta.json")
    sidecar_path.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return sidecar_path


def stage_aas_output(aas_path: Path, stage_dir: Path) -> dict[str, object]:
    stage_dir.mkdir(parents=True, exist_ok=True)
    staged_aas = stage_dir / aas_path.name
    shutil.copy2(aas_path, staged_aas)
    return {
        "enabled": True,
        "status": "staged",
        "aas": str(staged_aas),
        "aas_sha256": sha256_file(staged_aas),
    }


def print_map_summary(output: str, map_label: str) -> None:
    lines: list[str] = []
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith(INTERESTING_PREFIXES):
            lines.append(stripped)
            continue
        if any(re.search(rf"\b{metric}\s*=", stripped) for metric in INTERESTING_METRICS):
            lines.append(stripped)
            continue
        if any(re.fullmatch(rf"\d+\s+{re.escape(name)}", stripped) for name in TRAVEL_NAMES):
            lines.append(stripped)

    print(f"[q2aas] summary for {map_label}:")
    if lines:
        for line in lines:
            print(f"[q2aas]   {line}")
    else:
        print("[q2aas]   no summary lines were emitted by the tool")


def print_diagnostics_summary(diagnostics: dict[str, object], map_label: str) -> None:
    entities = diagnostics["entities"]["groups"]
    brush_contents = diagnostics["brush_contents"]
    origin_coverage = diagnostics["origin_coverage"]
    reachability = diagnostics["reachability_from_spawns"]
    movers = diagnostics["mover_entity_report"]

    print(f"[q2aas] diagnostics for {map_label}:")
    print(
        "[q2aas]   entities: "
        f"spawns={entities['spawn_points']['total']} "
        f"intermissions={entities['intermissions']['total']} "
        f"items={entities['items']['total']} "
        f"high_value={entities['high_value_items']['total']} "
        f"movers={entities['movers']['total']} "
        f"triggers={entities['triggers']['total']}"
    )
    print(
        "[q2aas]   origin coverage: "
        f"spawn_orphans={origin_coverage['spawn_points']['outside_aas_area_bounds']} "
        f"item_orphans={origin_coverage['items']['outside_aas_area_bounds']} "
        f"unreachable_high_value={reachability['unreachable_high_value_items']}"
    )
    print(
        "[q2aas]   contents/movers: "
        f"liquid_brushes={brush_contents['liquid_brushes']} "
        f"hazard_brushes={brush_contents['hazard_brushes']} "
        f"ladder_brushes={brush_contents['ladder_brushes']} "
        f"doors={len(movers['doors'])} "
        f"elevators={len(movers['elevators'])} "
        f"teleports={len(movers['teleports'])}"
    )


def validate_map_output(
    output: str,
    map_path: Path,
    aas_path: Path,
    require_reachability: bool,
) -> tuple[bool, list[str], dict[str, int], dict[str, int]]:
    ok = True
    warnings: list[str] = []
    metrics = extract_metrics(output)
    travel_counts = extract_travel_counts(output)

    if not aas_path.exists():
        message = f"expected output missing: {aas_path}"
        print(f"[q2aas] {message}", file=sys.stderr)
        return False, [message], metrics, travel_counts

    numareas = metrics.get("numareas")
    reachabilitysize = metrics.get("reachabilitysize")
    numclusters = metrics.get("numclusters")

    if numareas is None:
        message = f"could not parse numareas for {map_path}"
        print(f"[q2aas] {message}", file=sys.stderr)
        warnings.append(message)
        ok = False
    elif numareas <= 0:
        message = f"generated AAS has no areas for {map_path}"
        print(f"[q2aas] {message}", file=sys.stderr)
        warnings.append(message)
        ok = False

    if reachabilitysize == 0:
        message = "generated AAS has zero reachability"
        print(f"[q2aas] warning: {message}", file=sys.stderr)
        warnings.append(message)
    if numclusters == 0:
        message = "generated AAS has zero clusters"
        print(f"[q2aas] warning: {message}", file=sys.stderr)
        warnings.append(message)

    if require_reachability:
        if reachabilitysize is None or reachabilitysize <= 0:
            message = f"reachability required but absent for {map_path}"
            print(f"[q2aas] {message}", file=sys.stderr)
            warnings.append(message)
            ok = False
        if numclusters is None or numclusters <= 0:
            message = f"clusters required but absent for {map_path}"
            print(f"[q2aas] {message}", file=sys.stderr)
            warnings.append(message)
            ok = False

    print(f"[q2aas] output AAS: {aas_path}")
    return ok, warnings, metrics, travel_counts


def diagnostic_gate_record(
    required: bool,
    observed_ok: bool,
    messages: list[str],
    details: dict[str, object],
) -> dict[str, object]:
    if required:
        status = "passed" if observed_ok else "failed"
        passed: bool | None = observed_ok
    else:
        status = "not_required"
        passed = None
    return {
        "required": required,
        "status": status,
        "passed": passed,
        "observed_ok": observed_ok,
        "messages": messages,
        **details,
    }


def validate_diagnostic_requirements(
    diagnostics: dict[str, object],
    bsp_header: dict[str, object],
    map_path: Path,
    require_clean_bsp_lumps: bool,
    require_spawn_coverage: bool,
    require_item_coverage: bool,
    require_high_value_reachability: bool,
) -> tuple[bool, list[str], dict[str, object]]:
    ok = True
    failures: list[str] = []
    requirements: dict[str, object] = {}

    lump_issues = list(bsp_header.get("lump_issues", []))
    clean_lumps_ok = not lump_issues
    clean_lump_messages: list[str] = []
    if require_clean_bsp_lumps and not clean_lumps_ok:
        clean_lump_messages = [f"{map_path} has BSP lump table issues"]
        clean_lump_messages.extend(str(issue) for issue in lump_issues[:32])
    requirements["clean_bsp_lumps"] = diagnostic_gate_record(
        require_clean_bsp_lumps,
        clean_lumps_ok,
        clean_lump_messages,
        {
            "issue_count": len(lump_issues),
            "issues": lump_issues[:32],
        },
    )

    origin_coverage = diagnostics["origin_coverage"]
    spawn_coverage = origin_coverage["spawn_points"]
    spawn_failures: list[str] = []
    spawn_total = int(spawn_coverage["total"])
    spawn_missing = int(spawn_coverage["missing_origin"])
    spawn_outside = int(spawn_coverage["outside_aas_area_bounds"])
    if spawn_total <= 0:
        spawn_failures.append(f"{map_path} has no player spawn entities")
    if spawn_missing:
        spawn_failures.append(f"{map_path} has {spawn_missing} spawn entities without parseable origins")
    if spawn_outside:
        spawn_failures.append(f"{map_path} has {spawn_outside} spawn entities outside generated AAS area bounds")
    requirements["spawn_coverage"] = diagnostic_gate_record(
        require_spawn_coverage,
        not spawn_failures,
        spawn_failures,
        {
            "total": spawn_total,
            "mapped_to_aas_area": int(spawn_coverage["mapped_to_aas_area"]),
            "missing_origin": spawn_missing,
            "outside_aas_area_bounds": spawn_outside,
        },
    )

    item_coverage = origin_coverage["items"]
    item_failures: list[str] = []
    item_missing = int(item_coverage["missing_origin"])
    item_outside = int(item_coverage["outside_aas_area_bounds"])
    if item_missing:
        item_failures.append(f"{map_path} has {item_missing} item entities without parseable origins")
    if item_outside:
        item_failures.append(f"{map_path} has {item_outside} item entities outside generated AAS area bounds")
    requirements["item_coverage"] = diagnostic_gate_record(
        require_item_coverage,
        not item_failures,
        item_failures,
        {
            "total": int(item_coverage["total"]),
            "mapped_to_aas_area": int(item_coverage["mapped_to_aas_area"]),
            "missing_origin": item_missing,
            "outside_aas_area_bounds": item_outside,
        },
    )

    reachability = diagnostics["reachability_from_spawns"]
    high_value_failures: list[str] = []
    unreachable_high_value = int(reachability["unreachable_high_value_items"])
    if unreachable_high_value:
        high_value_failures.append(
            f"{map_path} has {unreachable_high_value} high-value pickups unreachable from spawn areas"
        )
    requirements["high_value_reachability"] = diagnostic_gate_record(
        require_high_value_reachability,
        not high_value_failures,
        high_value_failures,
        {
            "spawn_area_count": int(reachability["spawn_area_count"]),
            "reachable_area_count": int(reachability["reachable_area_count"]),
            "high_value_item_area_count": int(reachability["high_value_item_area_count"]),
            "unreachable_high_value_items": unreachable_high_value,
        },
    )

    for key, record in requirements.items():
        if record["status"] != "failed":
            continue
        ok = False
        for message in record["messages"]:
            failures.append(f"{key}: {message}")

    for failure in failures:
        print(f"[q2aas] diagnostic gate failed: {failure}", file=sys.stderr)

    return ok, failures, requirements


def baseline_gate_record(checks: dict[str, dict[str, object]]) -> dict[str, object]:
    required = bool(checks)
    failed = [name for name, check in checks.items() if check["status"] == "failed"]
    if required:
        status = "failed" if failed else "passed"
        passed: bool | None = not failed
    else:
        status = "not_required"
        passed = None
    return {
        "required": required,
        "status": status,
        "passed": passed,
        "failed": failed,
        "checks": checks,
    }


def validate_minimums(
    actual: dict[str, int],
    minimums: dict[str, int],
    kind: str,
    map_path: Path,
) -> tuple[dict[str, dict[str, object]], list[str]]:
    checks: dict[str, dict[str, object]] = {}
    failures: list[str] = []

    for name, minimum in sorted(minimums.items()):
        value = actual.get(name)
        if value is None:
            message = f"{map_path} missing {kind} '{name}' required at >= {minimum}"
            checks[name] = {
                "minimum": minimum,
                "actual": None,
                "status": "failed",
                "message": message,
            }
            failures.append(message)
            continue
        if value < minimum:
            message = f"{map_path} {kind} '{name}' is {value}, below required minimum {minimum}"
            checks[name] = {
                "minimum": minimum,
                "actual": value,
                "status": "failed",
                "message": message,
            }
            failures.append(message)
            continue
        checks[name] = {
            "minimum": minimum,
            "actual": value,
            "status": "passed",
            "message": "",
        }

    return checks, failures


def validate_baseline_requirements(
    metrics: dict[str, int],
    travel_counts: dict[str, int],
    map_path: Path,
    minimum_metrics: dict[str, int],
    minimum_travel_counts: dict[str, int],
) -> tuple[bool, list[str], dict[str, object]]:
    metric_checks, metric_failures = validate_minimums(metrics, minimum_metrics, "metric", map_path)
    travel_checks, travel_failures = validate_minimums(
        travel_counts,
        minimum_travel_counts,
        "travel count",
        map_path,
    )
    failures = metric_failures + travel_failures

    for failure in failures:
        print(f"[q2aas] baseline gate failed: {failure}", file=sys.stderr)

    return not failures, failures, {
        "minimum_metrics": baseline_gate_record(metric_checks),
        "minimum_travel_counts": baseline_gate_record(travel_checks),
    }


def cleanup_log(log_path: Path, keep_log: bool) -> None:
    if not keep_log and log_path.exists():
        try:
            log_path.unlink()
        except OSError as exc:
            print(f"[q2aas] warning: could not remove {log_path}: {exc}", file=sys.stderr)


def resolve_path(root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path
    if path.exists():
        return path.resolve()
    return root / path


def normalized_archive_member(member: str) -> tuple[str | None, str | None]:
    normalized = member.replace("\\", "/")
    archive_path = PurePosixPath(normalized)
    if not normalized or archive_path.is_absolute():
        return None, "archive_member must be a relative path inside the archive"
    if archive_path.parts and ":" in archive_path.parts[0]:
        return None, f"archive_member has an unsafe path component: {member}"
    if any(part in ("", ".", "..") for part in archive_path.parts):
        return None, f"archive_member has an unsafe path component: {member}"
    return archive_path.as_posix(), None


def archive_cache_root(cache_dir: Path, archive_path: Path) -> Path:
    archive_key = hashlib.sha256(str(archive_path.resolve()).encode("utf-8")).hexdigest()[:12]
    return cache_dir / f"{archive_path.stem}-{archive_key}"


def extract_packaged_map(
    archive_path: Path,
    archive_member: str,
    cache_dir: Path,
) -> tuple[Path | None, dict[str, object], list[str]]:
    errors: list[str] = []
    map_source: dict[str, object] = {
        "type": "archive",
        "archive": str(archive_path),
        "archive_member": archive_member,
        "extracted_path": None,
    }

    normalized_member, member_error = normalized_archive_member(archive_member)
    if member_error:
        errors.append(member_error)
        return None, map_source, errors
    assert normalized_member is not None
    map_source["archive_member"] = normalized_member

    if archive_path.suffix.lower() not in {".pkz", ".pak", ".pk3", ".zip"}:
        errors.append(f"archive path should be a .pkz/.pak/.pk3/.zip file: {archive_path}")
        return None, map_source, errors
    if not archive_path.is_file():
        errors.append(f"archive not found: {archive_path}")
        return None, map_source, errors

    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            members = {
                info.filename.replace("\\", "/"): info
                for info in archive.infolist()
                if not info.is_dir()
            }
            info = members.get(normalized_member)
            if info is None:
                lowered = normalized_member.lower()
                for candidate_name, candidate in members.items():
                    if candidate_name.lower() == lowered:
                        info = candidate
                        break
            if info is None:
                errors.append(f"archive member not found: {normalized_member} in {archive_path}")
                return None, map_source, errors

            extracted_path = archive_cache_root(cache_dir, archive_path).joinpath(
                *PurePosixPath(normalized_member).parts
            )
            extracted_path.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info, "r") as source, extracted_path.open("wb") as dest:
                shutil.copyfileobj(source, dest)

            map_source.update({
                "archive_sha256": sha256_file(archive_path),
                "archive_member_size": info.file_size,
                "archive_member_compressed_size": info.compress_size,
                "extracted_path": str(extracted_path),
            })
            return extracted_path, map_source, errors
    except zipfile.BadZipFile as exc:
        errors.append(f"archive is not a readable zip/pkz file: {archive_path}: {exc}")
    except OSError as exc:
        errors.append(f"could not extract {normalized_member} from {archive_path}: {exc}")

    return None, map_source, errors


def manifest_error(manifest_report: dict[str, object], message: str) -> None:
    errors = manifest_report["errors"]
    if isinstance(errors, list):
        errors.append(message)
    print(f"[q2aas] manifest error: {manifest_report['path']}: {message}", file=sys.stderr)


def validate_bool_field(entry: dict[str, object], name: str, default: bool, map_id: object) -> tuple[bool, list[str]]:
    if name not in entry:
        return default, []
    value = entry[name]
    if isinstance(value, bool):
        return value, []
    return default, [f"{name} for {map_id} must be a boolean"]


def normalize_string_list(
    raw: object,
    label: str,
    owner: object,
    *,
    required: bool = False,
) -> tuple[list[str], list[str]]:
    if raw is None:
        if required:
            return [], [f"{label} for {owner} must be an array of non-empty strings"]
        return [], []
    if not isinstance(raw, list):
        return [], [f"{label} for {owner} must be an array of non-empty strings"]

    values: list[str] = []
    errors: list[str] = []
    seen: set[str] = set()
    for index, value in enumerate(raw):
        if not isinstance(value, str) or not value:
            errors.append(f"{label}[{index}] for {owner} must be a non-empty string")
            continue
        if value in seen:
            continue
        seen.add(value)
        values.append(value)

    if required and not values:
        errors.append(f"{label} for {owner} must contain at least one value")
    return values, errors


def normalize_thresholds(
    raw: object,
    label: str,
    map_id: object,
    valid_names: set[str],
) -> tuple[dict[str, int], list[str]]:
    if raw is None:
        return {}, []
    if not isinstance(raw, dict):
        return {}, [f"{label} for {map_id} must be an object"]

    thresholds: dict[str, int] = {}
    errors: list[str] = []
    for name, value in raw.items():
        if not isinstance(name, str) or not name:
            errors.append(f"{label} for {map_id} contains a non-string or empty key")
            continue
        if name not in valid_names:
            errors.append(f"{label}.{name} for {map_id} is not a known validation key")
            continue
        if isinstance(value, bool) or not isinstance(value, int):
            errors.append(f"{label}.{name} for {map_id} must be a non-negative integer")
            continue
        threshold = value
        if threshold < 0:
            errors.append(f"{label}.{name} for {map_id} must be a non-negative integer")
            continue
        thresholds[name] = threshold

    return thresholds, errors


def normalize_reference_coverage(
    raw: object,
) -> tuple[list[dict[str, object]], list[str]]:
    if raw is None:
        return [], []
    if not isinstance(raw, list):
        return [], ["reference_coverage must be an array"]

    categories: list[dict[str, object]] = []
    errors: list[str] = []
    seen_ids: set[str] = set()
    for index, entry in enumerate(raw):
        owner = f"reference_coverage[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{owner} must be an object")
            continue

        unknown_keys = sorted(set(entry) - VALID_REFERENCE_COVERAGE_KEYS)
        for key in unknown_keys:
            errors.append(f"{owner} has unknown key '{key}'")

        raw_id = entry.get("id")
        if not isinstance(raw_id, str) or not raw_id:
            errors.append(f"{owner}.id must be a non-empty string")
            category_id = f"invalid-{index}"
        else:
            category_id = raw_id
            if category_id in seen_ids:
                errors.append(f"{owner}.id duplicates reference coverage id '{category_id}'")
            seen_ids.add(category_id)

        description = entry.get("description", "")
        if not isinstance(description, str):
            errors.append(f"{owner}.description must be a string")
            description = ""

        notes = entry.get("notes", "")
        if not isinstance(notes, str):
            errors.append(f"{owner}.notes must be a string")
            notes = ""

        map_ids, map_id_errors = normalize_string_list(
            entry.get("map_ids"),
            "map_ids",
            owner,
            required=True,
        )
        errors.extend(map_id_errors)

        minimum_validated_maps = entry.get("minimum_validated_maps", 1)
        if (
            isinstance(minimum_validated_maps, bool)
            or not isinstance(minimum_validated_maps, int)
            or minimum_validated_maps < 1
        ):
            errors.append(f"{owner}.minimum_validated_maps must be a positive integer")
            minimum_validated_maps = 1

        categories.append({
            "id": category_id,
            "description": description,
            "map_ids": map_ids,
            "minimum_validated_maps": minimum_validated_maps,
            "notes": notes,
        })

    return categories, errors


def build_reference_coverage_report(
    categories: list[dict[str, object]],
    map_status_by_id: dict[str, dict[str, object]],
) -> dict[str, object]:
    if not categories:
        return {
            "status": "not_configured",
            "category_count": 0,
            "incomplete_category_count": 0,
            "missing_map_count": 0,
            "missing_category_map_count": 0,
            "unique_missing_map_ids": [],
            "categories": [],
            "incomplete_categories": [],
            "missing_maps": [],
        }

    category_reports: list[dict[str, object]] = []
    incomplete_categories: list[str] = []
    missing_maps: list[dict[str, object]] = []

    for category in categories:
        category_id = str(category["id"])
        map_ids = [str(map_id) for map_id in category.get("map_ids", [])]
        candidate_reports: list[dict[str, object]] = []
        validated_count = 0

        for map_id in map_ids:
            map_status = map_status_by_id.get(map_id)
            if map_status is None:
                candidate = {
                    "id": map_id,
                    "status": "not_declared",
                }
            else:
                candidate = {
                    "id": map_id,
                    "status": map_status.get("status", "unknown"),
                    "path": map_status.get("path"),
                    "map_source": map_status.get("map_source"),
                    "required": map_status.get("required", False),
                    "coverage_categories": map_status.get("coverage_categories", []),
                }
            if candidate["status"] == "loaded":
                validated_count += 1
            else:
                missing_maps.append({
                    "category": category_id,
                    "id": map_id,
                    "status": candidate["status"],
                    "path": candidate.get("path"),
                    "map_source": candidate.get("map_source"),
                })
            candidate_reports.append(candidate)

        minimum_validated_maps = int(category.get("minimum_validated_maps", 1))
        category_status = "passed" if validated_count >= minimum_validated_maps else "incomplete"
        if category_status == "incomplete":
            incomplete_categories.append(category_id)

        category_reports.append({
            "id": category_id,
            "description": category.get("description", ""),
            "notes": category.get("notes", ""),
            "status": category_status,
            "validated_map_count": validated_count,
            "minimum_validated_maps": minimum_validated_maps,
            "candidate_maps": candidate_reports,
        })

    unique_missing_map_ids = sorted({
        str(entry["id"])
        for entry in missing_maps
        if entry.get("id")
    })

    return {
        "status": "incomplete" if incomplete_categories else "passed",
        "category_count": len(category_reports),
        "incomplete_category_count": len(incomplete_categories),
        "missing_map_count": len(unique_missing_map_ids),
        "missing_category_map_count": len(missing_maps),
        "unique_missing_map_ids": unique_missing_map_ids,
        "categories": category_reports,
        "incomplete_categories": incomplete_categories,
        "missing_maps": missing_maps,
    }


def summarize_manifest_reference_coverage(
    manifest_reports: list[dict[str, object]],
) -> dict[str, object]:
    coverage_reports = [
        report.get("reference_coverage")
        for report in manifest_reports
        if isinstance(report.get("reference_coverage"), dict)
    ]
    configured = [
        report
        for report in coverage_reports
        if report.get("status") != "not_configured"
    ]
    incomplete = [
        report
        for report in configured
        if report.get("status") == "incomplete"
    ]
    return {
        "status": (
            "not_configured"
            if not configured
            else "incomplete"
            if incomplete
            else "passed"
        ),
        "manifest_count": len(manifest_reports),
        "configured_manifest_count": len(configured),
        "incomplete_manifest_count": len(incomplete),
        "missing_map_count": sum(int(report.get("missing_map_count", 0)) for report in configured),
        "incomplete_categories": [
            category
            for report in configured
            for category in report.get("incomplete_categories", [])
            if isinstance(category, str)
        ],
    }


def print_reference_coverage_warnings(manifest_report: dict[str, object]) -> None:
    coverage = manifest_report.get("reference_coverage")
    if not isinstance(coverage, dict) or coverage.get("status") != "incomplete":
        return

    for category in coverage.get("categories", []):
        if not isinstance(category, dict) or category.get("status") != "incomplete":
            continue
        print(
            "[q2aas] warning: reference coverage incomplete: "
            f"{category.get('id')} has {category.get('validated_map_count')} validated maps, "
            f"needs {category.get('minimum_validated_maps')}",
        )
        for candidate in category.get("candidate_maps", []):
            if not isinstance(candidate, dict) or candidate.get("status") == "loaded":
                continue
            path = candidate.get("path")
            suffix = f" ({path})" if path else ""
            print(
                "[q2aas] warning:   missing reference map "
                f"{candidate.get('id')}: {candidate.get('status')}{suffix}",
            )


def load_manifest(
    root: Path,
    manifest: Path,
    skip_missing: bool,
    packaged_map_cache_dir: Path,
) -> tuple[list[dict[str, object]], bool, dict[str, object]]:
    manifest_path = resolve_path(root, manifest).resolve()
    manifest_report: dict[str, object] = {
        "path": str(manifest_path),
        "schema": None,
        "version": None,
        "task_ids": [],
        "map_count": 0,
        "loaded_map_count": 0,
        "pending_reference_maps": [],
        "reference_coverage": {
            "status": "not_configured",
            "category_count": 0,
            "incomplete_category_count": 0,
            "missing_map_count": 0,
            "missing_category_map_count": 0,
            "unique_missing_map_ids": [],
            "categories": [],
            "incomplete_categories": [],
            "missing_maps": [],
        },
        "declared_maps": [],
        "errors": [],
        "skipped_maps": [],
    }
    try:
        with manifest_path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        manifest_error(manifest_report, f"could not read manifest JSON: {exc}")
        return [], False, manifest_report

    if not isinstance(data, dict):
        manifest_error(manifest_report, "manifest root must be a JSON object")
        return [], False, manifest_report

    unknown_keys = sorted(set(data) - VALID_MANIFEST_KEYS)
    for key in unknown_keys:
        manifest_error(manifest_report, f"unknown root key '{key}'")

    schema = data.get("schema")
    manifest_report["schema"] = schema
    if schema != VALIDATION_MANIFEST_SCHEMA:
        manifest_error(
            manifest_report,
            f"schema must be '{VALIDATION_MANIFEST_SCHEMA}'",
        )

    version = data.get("version")
    manifest_report["version"] = version
    if isinstance(version, bool) or version != VALIDATION_MANIFEST_VERSION:
        manifest_error(manifest_report, f"version must be {VALIDATION_MANIFEST_VERSION}")

    task_ids = data.get("task_ids", [])
    if not isinstance(task_ids, list) or not all(isinstance(task_id, str) and task_id for task_id in task_ids):
        manifest_error(manifest_report, "task_ids must be an array of non-empty strings")
        task_ids = []
    manifest_report["task_ids"] = task_ids

    pending_reference_maps = data.get("pending_reference_maps", [])
    if not isinstance(pending_reference_maps, list) or not all(
        isinstance(reference, str) and reference for reference in pending_reference_maps
    ):
        manifest_error(manifest_report, "pending_reference_maps must be an array of non-empty strings")
        pending_reference_maps = []
    manifest_report["pending_reference_maps"] = pending_reference_maps

    reference_coverage, reference_coverage_errors = normalize_reference_coverage(
        data.get("reference_coverage")
    )
    for error in reference_coverage_errors:
        manifest_error(manifest_report, error)

    manifest_maps = data.get("maps", [])
    if not isinstance(manifest_maps, list):
        manifest_error(manifest_report, "maps must be an array")
        manifest_maps = []
    manifest_report["map_count"] = len(manifest_maps)

    maps: list[dict[str, object]] = []
    map_status_by_id: dict[str, dict[str, object]] = {}
    ok = not bool(manifest_report["errors"])
    for index, entry in enumerate(manifest_maps):
        if not isinstance(entry, dict):
            manifest_error(manifest_report, f"maps[{index}] must be an object")
            ok = False
            continue

        unknown_map_keys = sorted(set(entry) - VALID_MANIFEST_MAP_KEYS)
        for key in unknown_map_keys:
            manifest_error(manifest_report, f"maps[{index}] has unknown key '{key}'")
            ok = False

        raw_path_value = entry.get("path")
        raw_archive_value = entry.get("archive")
        raw_archive_member_value = entry.get("archive_member")
        has_path = isinstance(raw_path_value, str) and bool(raw_path_value)
        has_archive = isinstance(raw_archive_value, str) and bool(raw_archive_value)
        has_archive_member = isinstance(raw_archive_member_value, str) and bool(raw_archive_member_value)

        if has_path and (has_archive or has_archive_member):
            manifest_error(
                manifest_report,
                f"maps[{index}] must use either path or archive/archive_member, not both",
            )
            ok = False
            continue
        if not has_path and not (has_archive and has_archive_member):
            manifest_error(
                manifest_report,
                f"maps[{index}] must define path or archive plus archive_member",
            )
            ok = False
            continue
        if has_archive != has_archive_member:
            manifest_error(
                manifest_report,
                f"maps[{index}] archive-backed entries require both archive and archive_member",
            )
            ok = False
            continue

        default_id_source = raw_path_value if has_path else raw_archive_member_value
        assert isinstance(default_id_source, str)
        raw_map_id = entry.get("id", Path(default_id_source).stem)
        if not isinstance(raw_map_id, str) or not raw_map_id:
            manifest_error(manifest_report, f"maps[{index}].id must be a non-empty string")
            ok = False
            continue
        map_id = raw_map_id

        if has_archive:
            assert isinstance(raw_archive_value, str)
            assert isinstance(raw_archive_member_value, str)
            archive_path = resolve_path(root, Path(raw_archive_value)).resolve()
            extracted_path, map_source, archive_errors = extract_packaged_map(
                archive_path,
                raw_archive_member_value,
                packaged_map_cache_dir,
            )
            for error in archive_errors:
                manifest_error(manifest_report, f"{map_id}: {error}")
            if archive_errors or extracted_path is None:
                ok = False
                continue
            else:
                map_path = extracted_path.resolve()
                missing = not map_path.exists()
        else:
            assert isinstance(raw_path_value, str)
            raw_path = Path(raw_path_value)
            map_path = resolve_path(root, raw_path).resolve()
            map_source = {
                "type": "file",
                "path": str(map_path),
            }
            missing = not map_path.exists()
        required, bool_errors = validate_bool_field(entry, "required", True, map_id)
        require_reachability, reachability_errors = validate_bool_field(
            entry,
            "require_reachability",
            False,
            map_id,
        )
        require_clean_bsp_lumps, clean_lump_errors = validate_bool_field(
            entry,
            "require_clean_bsp_lumps",
            False,
            map_id,
        )
        require_spawn_coverage, spawn_errors = validate_bool_field(
            entry,
            "require_spawn_coverage",
            False,
            map_id,
        )
        require_item_coverage, item_errors = validate_bool_field(
            entry,
            "require_item_coverage",
            False,
            map_id,
        )
        require_high_value_reachability, high_value_errors = validate_bool_field(
            entry,
            "require_high_value_reachability",
            False,
            map_id,
        )
        coverage_categories, coverage_category_errors = normalize_string_list(
            entry.get("coverage_categories"),
            "coverage_categories",
            map_id,
        )
        minimum_metrics, metric_errors = normalize_thresholds(
            entry.get("minimum_metrics"),
            "minimum_metrics",
            map_id,
            set(INTERESTING_METRICS),
        )
        minimum_travel_counts, travel_errors = normalize_thresholds(
            entry.get("minimum_travel_counts"),
            "minimum_travel_counts",
            map_id,
            set(TRAVEL_NAMES),
        )

        notes = entry.get("notes", "")
        note_errors: list[str] = []
        if not isinstance(notes, str):
            notes = ""
            note_errors.append(f"notes for {map_id} must be a string")

        for error in (
            bool_errors
            + reachability_errors
            + clean_lump_errors
            + spawn_errors
            + item_errors
            + high_value_errors
            + coverage_category_errors
            + metric_errors
            + travel_errors
            + note_errors
        ):
            manifest_error(manifest_report, error)
            ok = False

        map_report = {
            "id": map_id,
            "path": str(map_path),
            "map_source": map_source,
            "required": required,
            "coverage_categories": coverage_categories,
            "status": "missing" if missing else "loaded",
        }
        map_status_by_id[map_id] = map_report

        if missing:
            message = f"manifest map missing: {map_path}"
            if skip_missing:
                print(f"[q2aas] warning: {message}; skipping")
                map_report["status"] = "skipped_missing"
                skipped_maps = manifest_report["skipped_maps"]
                if isinstance(skipped_maps, list):
                    skipped_maps.append({
                        "id": map_id,
                        "path": str(map_path),
                        "map_source": map_source,
                        "coverage_categories": coverage_categories,
                        "reason": "missing",
                        "required": required,
                    })
                continue
            print(f"[q2aas] {message}", file=sys.stderr)
            if required:
                ok = False
            continue

        maps.append({
            "id": map_id,
            "path": map_path,
            "map_source": map_source,
            "require_reachability": require_reachability,
            "require_clean_bsp_lumps": require_clean_bsp_lumps,
            "require_spawn_coverage": require_spawn_coverage,
            "require_item_coverage": require_item_coverage,
            "require_high_value_reachability": require_high_value_reachability,
            "coverage_categories": coverage_categories,
            "minimum_metrics": minimum_metrics,
            "minimum_travel_counts": minimum_travel_counts,
            "notes": notes,
            "source": str(manifest_path),
        })

    manifest_report["loaded_map_count"] = len(maps)
    manifest_report["declared_maps"] = list(map_status_by_id.values())
    manifest_report["reference_coverage"] = build_reference_coverage_report(
        reference_coverage,
        map_status_by_id,
    )
    return maps, ok, manifest_report


def run_invalid_input_smoke(tool: Path, cfg: Path, output: Path, keep_log: bool) -> dict[str, object]:
    invalid_path = output / "invalid-input-smoke.bsp"
    invalid_path.write_bytes(b"BAD!\x01\x00\x00\x00not a valid WORR/Q2 BSP smoke input\n")

    log_path = output / "bspc.log"
    command = [str(tool), "-cfg", str(cfg), "-output", str(output), "-bsp2aas", str(invalid_path)]
    result = run_command(command, output)

    failed_as_expected = result.returncode != 0
    output_lines = result.stdout.splitlines()
    error_line = next((line for line in output_lines if "ERROR:" in line), "")
    if failed_as_expected:
        detail = f": {error_line}" if error_line else ""
        print(f"[q2aas] invalid input smoke failed as expected with exit {result.returncode}{detail}")
    else:
        print("[q2aas] invalid input smoke unexpectedly succeeded", file=sys.stderr)
        print(result.stdout, end="")

    if invalid_path.exists():
        invalid_path.unlink()
    cleanup_log(log_path, keep_log)

    return {
        "path": str(invalid_path),
        "returncode": result.returncode,
        "status": "passed" if failed_as_expected else "failed",
        "output_excerpt": output_lines[:20],
    }


def run_manifest_schema_smoke(root: Path, output: Path, packaged_map_cache_dir: Path) -> dict[str, object]:
    invalid_path = output / "invalid-manifest-schema-smoke.json"
    invalid_manifest = {
        "schema": VALIDATION_MANIFEST_SCHEMA,
        "version": VALIDATION_MANIFEST_VERSION,
        "task_ids": ["FR-04-T11"],
        "reference_coverage": [
            {
                "id": "bad-reference-minimum",
                "map_ids": ["invalid-threshold-smoke"],
                "minimum_validated_maps": 0,
            },
            {
                "id": "bad-reference-map-list",
                "map_ids": "invalid-threshold-smoke",
            },
        ],
        "maps": [
            {
                "id": "invalid-threshold-smoke",
                "path": ".install/basew/maps/mm-rage.bsp",
                "minimum_metrics": {
                    "not_a_metric": 1,
                },
                "minimum_travel_counts": {
                    "walk": "468",
                },
            },
            {
                "id": "archive-and-path-smoke",
                "path": ".install/basew/maps/mm-rage.bsp",
                "archive": ".tmp/q2aas/no-such.pkz",
                "archive_member": "maps/mm-rage.bsp",
            },
            {
                "id": "archive-missing-member-smoke",
                "archive": ".tmp/q2aas/no-such.pkz",
            },
            {
                "id": "archive-absolute-member-smoke",
                "archive": ".tmp/q2aas/no-such.pkz",
                "archive_member": "/maps/mm-rage.bsp",
            },
            {
                "id": "archive-traversal-member-smoke",
                "archive": ".tmp/q2aas/no-such.pkz",
                "archive_member": "../maps/mm-rage.bsp",
            },
        ],
    }
    invalid_path.write_text(json.dumps(invalid_manifest, indent=2) + "\n", encoding="utf-8")

    maps, ok, manifest_report = load_manifest(
        root,
        invalid_path,
        skip_missing=False,
        packaged_map_cache_dir=packaged_map_cache_dir,
    )
    errors = manifest_report.get("errors", [])
    error_messages = [str(error) for error in errors] if isinstance(errors, list) else []
    expected_error_fragments = (
        "minimum_metrics.not_a_metric",
        "minimum_travel_counts.walk",
        "must use either path or archive/archive_member",
        "must define path or archive plus archive_member",
        "archive_member must be a relative path inside the archive",
        "archive_member has an unsafe path component",
        "reference_coverage[0].minimum_validated_maps",
        "map_ids for reference_coverage[1]",
    )
    failed_as_expected = not ok and all(
        any(fragment in message for message in error_messages)
        for fragment in expected_error_fragments
    )

    if failed_as_expected:
        print("[q2aas] manifest schema smoke failed as expected")
    else:
        print("[q2aas] manifest schema smoke unexpectedly passed", file=sys.stderr)
        for message in error_messages:
            print(f"[q2aas]   manifest error: {message}", file=sys.stderr)

    if invalid_path.exists():
        invalid_path.unlink()

    return {
        "path": str(invalid_path),
        "status": "passed" if failed_as_expected else "failed",
        "loaded_map_count": len(maps),
        "expected_error_fragments": list(expected_error_fragments),
        "manifest_report": manifest_report,
    }


def create_package_map_smoke_spec(
    root: Path,
    output: Path,
    source_bsp: Path,
    packaged_map_cache_dir: Path,
) -> tuple[dict[str, object] | None, dict[str, object], bool]:
    source_bsp = resolve_path(root, source_bsp).resolve()
    smoke_archive = output / "package-map-smoke.pkz"
    archive_member = f"maps/{source_bsp.name}"
    report: dict[str, object] = {
        "status": "failed",
        "source_bsp": str(source_bsp),
        "archive": str(smoke_archive),
        "archive_member": archive_member,
        "map_source": None,
        "errors": [],
    }
    errors = report["errors"]
    assert isinstance(errors, list)

    if not source_bsp.is_file():
        errors.append(f"package map smoke source BSP not found: {source_bsp}")
        return None, report, False

    try:
        smoke_archive.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(smoke_archive, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.write(source_bsp, archive_member)
    except OSError as exc:
        errors.append(f"could not create package map smoke archive: {exc}")
        return None, report, False

    extracted_path, map_source, extract_errors = extract_packaged_map(
        smoke_archive,
        archive_member,
        packaged_map_cache_dir,
    )
    report["map_source"] = map_source
    errors.extend(extract_errors)
    if extract_errors or extracted_path is None:
        return None, report, False

    report["status"] = "created"
    spec = {
        "id": f"package-map-smoke-{source_bsp.stem}",
        "path": extracted_path.resolve(),
        "output_stem": f"package-map-smoke-{source_bsp.stem}",
        "map_source": map_source,
        "require_reachability": True,
        "require_clean_bsp_lumps": True,
        "require_spawn_coverage": True,
        "require_item_coverage": True,
        "require_high_value_reachability": True,
        "coverage_categories": ["package_map_extraction_smoke"],
        "minimum_metrics": {},
        "minimum_travel_counts": {},
        "notes": "Scratch pkz smoke generated from the current staged BSP to validate package-backed map extraction.",
        "source": "package_map_smoke",
    }
    return spec, report, True


def main() -> int:
    root = repo_root()

    parser = argparse.ArgumentParser(
        description="Validate the WORR Quake II AAS generator preset.",
    )
    parser.add_argument(
        "--build-dir",
        default="builddir-win" if os.name == "nt" else "builddir",
        help="Meson build directory containing the worr_q2aas target.",
    )
    parser.add_argument(
        "--tool",
        type=Path,
        default=None,
        help="Path to the worr_q2aas executable. Defaults to the selected build directory.",
    )
    parser.add_argument(
        "--cfg",
        type=Path,
        default=root / "tools" / "q2aas" / "cfg" / "worr_q2.cfg",
        help="AAS generator cfg file to load.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=root / ".tmp" / "q2aas",
        help="Scratch output directory for logs and generated AAS files.",
    )
    parser.add_argument(
        "--map",
        dest="maps",
        type=Path,
        action="append",
        default=[],
        help="BSP map to convert. May be passed more than once.",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        action="append",
        default=[],
        help="JSON validation manifest with a maps array. May be passed more than once.",
    )
    parser.add_argument(
        "--skip-missing-manifest-maps",
        action="store_true",
        help="Skip missing manifest maps instead of failing validation.",
    )
    parser.add_argument(
        "--allow-empty-map-set",
        action="store_true",
        help="Allow manifest/direct map selection to resolve to no maps.",
    )
    parser.add_argument(
        "--require-reference-coverage",
        action="store_true",
        help="Fail when manifest reference_coverage categories do not have enough validated maps.",
    )
    parser.add_argument(
        "--threads",
        default="1",
        help="Thread count passed to worr_q2aas for deterministic local smoke runs.",
    )
    parser.add_argument(
        "--keep-log",
        action="store_true",
        help="Keep the generated bspc.log file in the scratch directory.",
    )
    parser.add_argument(
        "--verbose-tool",
        action="store_true",
        help="Print full tool output for map conversions instead of the validation summary.",
    )
    parser.add_argument(
        "--require-reachability",
        action="store_true",
        help="Fail map conversions when reachability data or clusters are absent.",
    )
    parser.add_argument(
        "--require-q2-bsp",
        action="store_true",
        help="Fail selected maps before conversion unless they have a Quake II IBSP38 header.",
    )
    parser.add_argument(
        "--require-clean-bsp-lumps",
        action="store_true",
        help="Fail selected maps when the Quake II BSP lump table has invalid offsets or lengths.",
    )
    parser.add_argument(
        "--require-spawn-coverage",
        action="store_true",
        help="Fail selected maps when player spawns are missing origins or do not map to generated AAS areas.",
    )
    parser.add_argument(
        "--require-item-coverage",
        action="store_true",
        help="Fail selected maps when item origins do not map to generated AAS areas.",
    )
    parser.add_argument(
        "--require-high-value-reachability",
        action="store_true",
        help="Fail selected maps when high-value pickups are unreachable from generated spawn areas.",
    )
    parser.add_argument(
        "--invalid-input-smoke",
        action="store_true",
        help="Run an expected-failure smoke check against a deliberately invalid BSP.",
    )
    parser.add_argument(
        "--manifest-schema-smoke",
        action="store_true",
        help="Run an expected-failure smoke check against a deliberately invalid validation manifest.",
    )
    parser.add_argument(
        "--package-map-smoke",
        action="store_true",
        help="Create a scratch pkz from a staged BSP and validate archive-backed map extraction/conversion.",
    )
    parser.add_argument(
        "--package-map-smoke-source",
        type=Path,
        default=root / ".install" / "basew" / "maps" / "mm-rage.bsp",
        help="Source BSP used by --package-map-smoke. Defaults to .install/basew/maps/mm-rage.bsp.",
    )
    parser.add_argument(
        "--packaged-map-cache-dir",
        type=Path,
        default=None,
        help="Scratch directory for BSPs extracted from pkz/pak archives. Defaults to <output>/packaged-maps.",
    )
    parser.add_argument(
        "--write-aas-metadata",
        action="store_true",
        help="Write deterministic <map>.aas.meta.json sidecars beside generated AAS files.",
    )
    parser.add_argument(
        "--stage-aas",
        action="store_true",
        help="Copy validated generated AAS files into the selected staging directory.",
    )
    parser.add_argument(
        "--stage-aas-dir",
        type=Path,
        default=root / ".install" / "basew" / "maps",
        help="Destination directory for --stage-aas. Defaults to .install/basew/maps.",
    )
    parser.add_argument(
        "--report-json",
        type=Path,
        default=None,
        help="Write structured validation results to this JSON file.",
    )
    args = parser.parse_args()

    tool = resolve_path(root, args.tool if args.tool else default_tool(root, args.build_dir)).resolve()
    cfg = resolve_path(root, args.cfg).resolve()
    output = resolve_path(root, args.output).resolve()
    stage_aas_dir = resolve_path(root, args.stage_aas_dir).resolve()
    packaged_map_cache_dir = (
        resolve_path(root, args.packaged_map_cache_dir).resolve()
        if args.packaged_map_cache_dir
        else output / "packaged-maps"
    )
    map_specs: list[dict[str, object]] = [
        {
            "id": path.stem,
            "path": resolve_path(root, path).resolve(),
            "map_source": {
                "type": "file",
                "path": str(resolve_path(root, path).resolve()),
            },
            "require_reachability": args.require_reachability,
            "require_clean_bsp_lumps": args.require_clean_bsp_lumps,
            "require_spawn_coverage": args.require_spawn_coverage,
            "require_item_coverage": args.require_item_coverage,
            "require_high_value_reachability": args.require_high_value_reachability,
            "coverage_categories": [],
            "minimum_metrics": {},
            "minimum_travel_counts": {},
            "notes": "",
            "source": "cli",
        }
        for path in args.maps
    ]

    manifest_ok = True
    manifest_reports: list[dict[str, object]] = []
    for manifest in args.manifest:
        manifest_maps, ok, manifest_report = load_manifest(
            root,
            manifest,
            args.skip_missing_manifest_maps,
            packaged_map_cache_dir,
        )
        map_specs.extend(manifest_maps)
        manifest_reports.append(manifest_report)
        manifest_ok = manifest_ok and ok
        print_reference_coverage_warnings(manifest_report)

    reference_coverage_summary = summarize_manifest_reference_coverage(manifest_reports)
    if (
        args.require_reference_coverage
        and reference_coverage_summary["status"] == "incomplete"
    ):
        print("[q2aas] reference coverage is incomplete", file=sys.stderr)
        manifest_ok = False

    package_map_smoke_report: dict[str, object] | None = None
    if args.package_map_smoke:
        output.mkdir(parents=True, exist_ok=True)
        smoke_spec, package_map_smoke_report, package_smoke_ok = create_package_map_smoke_spec(
            root,
            output,
            args.package_map_smoke_source,
            packaged_map_cache_dir,
        )
        if smoke_spec is not None:
            map_specs.append(smoke_spec)
        manifest_ok = manifest_ok and package_smoke_ok

    if not tool.exists():
        print(f"[q2aas] missing tool: {tool}", file=sys.stderr)
        return 2
    if not cfg.exists():
        print(f"[q2aas] missing cfg: {cfg}", file=sys.stderr)
        return 2
    if not manifest_ok:
        return 2

    missing_maps = [spec["path"] for spec in map_specs if not Path(spec["path"]).exists()]
    if missing_maps:
        for path in missing_maps:
            print(f"[q2aas] missing map: {path}", file=sys.stderr)
        return 2
    if (args.maps or args.manifest) and not map_specs and not args.allow_empty_map_set:
        print("[q2aas] no maps selected for validation", file=sys.stderr)
        return 2

    output.mkdir(parents=True, exist_ok=True)
    tool_sha256 = sha256_file(tool)
    cfg_sha256 = sha256_file(cfg)
    report: dict[str, object] = {
        "schema": "worr-q2aas-validation-v1",
        "tool": str(tool),
        "tool_sha256": tool_sha256,
        "tool_version": None,
        "cfg": str(cfg),
        "cfg_sha256": cfg_sha256,
        "output": str(output),
        "reproducibility": {
            "generation_time": None,
            "generation_time_policy": "Omitted by default. Use hashes and metadata sidecars for deterministic identity.",
        },
        "manifests": manifest_reports,
        "reference_coverage": reference_coverage_summary,
        "maps": [],
    }
    if package_map_smoke_report is not None:
        report["package_map_smoke"] = package_map_smoke_report

    log_path = output / "bspc.log"
    if log_path.exists():
        cleanup_log(log_path, False)

    if not map_specs:
        command = [str(tool), "-cfg", str(cfg)]
        result = run_command(command, output)
        print(result.stdout, end="")
        version_line = extract_tool_version_line(result.stdout)
        if version_line:
            report["tool_version"] = version_line
        if result.returncode != 0:
            cleanup_log(log_path, args.keep_log)
            return result.returncode
    else:
        failed = False
        for spec in map_specs:
            map_path = Path(spec["path"])
            require_reachability = args.require_reachability or bool(spec["require_reachability"])
            require_clean_bsp_lumps = (
                args.require_clean_bsp_lumps or bool(spec["require_clean_bsp_lumps"])
            )
            require_spawn_coverage = (
                args.require_spawn_coverage or bool(spec["require_spawn_coverage"])
            )
            require_item_coverage = (
                args.require_item_coverage or bool(spec["require_item_coverage"])
            )
            require_high_value_reachability = (
                args.require_high_value_reachability or bool(spec["require_high_value_reachability"])
            )
            minimum_metrics = spec["minimum_metrics"] if isinstance(spec["minimum_metrics"], dict) else {}
            minimum_travel_counts = (
                spec["minimum_travel_counts"] if isinstance(spec["minimum_travel_counts"], dict) else {}
            )
            map_sha256 = sha256_file(map_path)
            bsp_header = inspect_q2_bsp(map_path)
            if args.require_q2_bsp and not bool(bsp_header.get("valid_q2_bsp")):
                print(
                    f"[q2aas] map is not a Quake II IBSP38 BSP: {map_path}",
                    file=sys.stderr,
                )
                print(
                    f"[q2aas] detected format={bsp_header.get('format')} "
                    f"ident={bsp_header.get('ident')} version={bsp_header.get('version')}",
                    file=sys.stderr,
                )
                return 2
            if bsp_header.get("lump_issues"):
                print(f"[q2aas] BSP lump warnings for {map_path}:")
                for issue in bsp_header["lump_issues"]:
                    print(f"[q2aas]   {issue}")

            command = [
                str(tool),
                "-cfg", str(cfg),
                "-threads", str(args.threads),
                "-output", str(output),
            ]
            command.extend(["-bsp2aas", str(map_path)])

            result = run_command(command, output)
            version_line = extract_tool_version_line(result.stdout)
            if version_line and report["tool_version"] is None:
                report["tool_version"] = version_line
            if args.verbose_tool or result.returncode != 0:
                print(result.stdout, end="")
            else:
                print_map_summary(result.stdout, str(spec["id"]))

            if result.returncode != 0:
                cleanup_log(log_path, args.keep_log)
                return result.returncode

            output_stem = str(spec.get("output_stem", map_path.stem))
            aas_path = output / f"{output_stem}.aas"
            tool_aas_path = output / f"{map_path.stem}.aas"
            if tool_aas_path != aas_path and tool_aas_path.exists():
                shutil.copy2(tool_aas_path, aas_path)
            ok, warnings, metrics, travel_counts = validate_map_output(
                result.stdout,
                map_path,
                aas_path,
                require_reachability,
            )
            aas_sha256 = sha256_file(aas_path)
            baseline_ok, baseline_warnings, baseline_requirements = validate_baseline_requirements(
                metrics,
                travel_counts,
                map_path,
                minimum_metrics,
                minimum_travel_counts,
            )
            warnings.extend(baseline_warnings)
            ok = ok and baseline_ok
            aas_header = decode_aas_header(aas_path)
            diagnostics = build_map_diagnostics(map_path, bsp_header, aas_path, aas_header)
            print_diagnostics_summary(diagnostics, str(spec["id"]))
            diagnostic_ok, diagnostic_warnings, diagnostic_requirements = validate_diagnostic_requirements(
                diagnostics,
                bsp_header,
                map_path,
                require_clean_bsp_lumps,
                require_spawn_coverage,
                require_item_coverage,
                require_high_value_reachability,
            )
            warnings.extend(diagnostic_warnings)
            ok = ok and diagnostic_ok
            metadata = build_aas_metadata(
                tool,
                cfg,
                output,
                command,
                spec["id"],
                map_path,
                aas_path,
                tool_sha256,
                cfg_sha256,
                map_sha256,
                aas_sha256,
                version_line or report["tool_version"],
                bsp_header,
                aas_header,
                diagnostics,
                spec.get("map_source", {"type": "file", "path": str(map_path)}),
            )
            metadata_sidecar = None
            if args.write_aas_metadata:
                metadata_sidecar = write_metadata_sidecar(aas_path, metadata)
                print(f"[q2aas] metadata: {metadata_sidecar}")

            staged_output = {"enabled": False}
            if args.stage_aas:
                if ok:
                    staged_output = stage_aas_output(aas_path, stage_aas_dir)
                    print(f"[q2aas] staged AAS: {staged_output['aas']}")
                else:
                    staged_output = {
                        "enabled": True,
                        "status": "skipped_failed_validation",
                        "directory": str(stage_aas_dir),
                    }

            report["maps"].append({
                "id": spec["id"],
                "path": str(map_path),
                "output_stem": output_stem,
                "map_source": spec.get("map_source", {"type": "file", "path": str(map_path)}),
                "bsp_sha256": map_sha256,
                "bsp_header": bsp_header,
                "aas": str(aas_path),
                "tool_aas": str(tool_aas_path),
                "aas_sha256": aas_sha256,
                "aas_header": aas_header,
                "diagnostics": diagnostics,
                "diagnostic_requirements": diagnostic_requirements,
                "baseline_requirements": baseline_requirements,
                "metadata_sidecar": str(metadata_sidecar) if metadata_sidecar else None,
                "staged_output": staged_output,
                "required_reachability": require_reachability,
                "required_clean_bsp_lumps": require_clean_bsp_lumps,
                "required_spawn_coverage": require_spawn_coverage,
                "required_item_coverage": require_item_coverage,
                "required_high_value_reachability": require_high_value_reachability,
                "coverage_categories": spec.get("coverage_categories", []),
                "minimum_metrics": minimum_metrics,
                "minimum_travel_counts": minimum_travel_counts,
                "status": "passed" if ok else "failed",
                "metrics": metrics,
                "travel_counts": travel_counts,
                "warnings": warnings,
                "notes": spec["notes"],
                "source": spec["source"],
            })
            if not ok:
                failed = True
        if failed:
            cleanup_log(log_path, args.keep_log)
            return 1

    if args.invalid_input_smoke:
        invalid_result = run_invalid_input_smoke(tool, cfg, output, args.keep_log)
        report["invalid_input_smoke"] = invalid_result
        if invalid_result["status"] != "passed":
            return 1

    if args.manifest_schema_smoke:
        manifest_schema_result = run_manifest_schema_smoke(root, output, packaged_map_cache_dir)
        report["manifest_schema_smoke"] = manifest_schema_result
        if manifest_schema_result["status"] != "passed":
            return 1

    cleanup_log(log_path, args.keep_log)

    if args.report_json:
        report_path = resolve_path(root, args.report_json).resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"[q2aas] report: {report_path}")

    print(f"[q2aas] scratch output: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
