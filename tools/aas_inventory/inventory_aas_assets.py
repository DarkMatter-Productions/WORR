#!/usr/bin/env python3
"""Inventory WORR BSP/AAS map assets and conversion gaps."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any


REPORT_SCHEMA = "worr-aas-asset-inventory-v1"
TASK_IDS = ("FR-04-T11", "FR-04-T16", "DV-07-T06")

ASSET_EXTENSIONS = {
    ".bsp": "bsp",
    ".aas": "aas",
    ".map": "map_source",
}
ARCHIVE_EXTENSIONS = {".pkz", ".zip", ".pak"}
DEFAULT_SCAN_ROOTS = ("assets", ".install", "refs")
DEFAULT_MANIFEST = "tools/q2aas/validation_manifest.json"
DEFAULT_REPORT_JSON = ".tmp/aas_inventory/asset-inventory.json"
DEFAULT_AVAILABLE_REFERENCE_MANIFEST = ".tmp/q2aas/available-reference-validation-manifest.json"
DEFAULT_REFERENCE_PATHS = (
    r"E:\_SOURCE\_CODE\Quake-III-Arena-master",
    r"E:\_SOURCE\_CODE\Quake-III-Arena-master\code\bspc",
    r"E:\_SOURCE\_CODE\Quake3e-master",
    r"E:\_SOURCE\_CODE\baseq3a-master",
)
REFERENCE_SELECTION_SCHEMA = "worr-aas-reference-validation-selection-v1"
MANIFEST_MAP_CARRY_KEYS = (
    "require_reachability",
    "require_clean_bsp_lumps",
    "require_spawn_coverage",
    "require_item_coverage",
    "require_high_value_reachability",
    "coverage_categories",
    "minimum_metrics",
    "minimum_travel_counts",
)

REFERENCE_COVERAGE_FEATURES = (
    "water",
    "slime",
    "lava",
    "teleport",
    "elevator",
    "door",
)

PAK_HEADER = struct.Struct("<4sii")
PAK_ENTRY = struct.Struct("<56sii")
LOCATION_CONTAINER_PRIORITY = {
    "loose": 0,
    "zip": 1,
    "pak": 2,
}
Q2AAS_VALIDATION_SUPPORTED_CONTAINERS = {"loose", "zip"}

Q2AAS_TOOL_DIR = Path(__file__).resolve().parents[1] / "q2aas"
if str(Q2AAS_TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(Q2AAS_TOOL_DIR))
try:
    import validate_worr_q2aas as q2aas_validator
except Exception:  # pragma: no cover - inventory still works without q2aas helpers.
    q2aas_validator = None


@dataclass
class AssetLocation:
    container: str
    path: str
    size: int
    member: str | None = None

    def to_json(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "container": self.container,
            "path": self.path,
            "size": self.size,
        }
        if self.member is not None:
            payload["member"] = self.member
        return payload


@dataclass
class MapAssets:
    map_id: str
    bsp: list[AssetLocation] = field(default_factory=list)
    aas: list[AssetLocation] = field(default_factory=list)
    map_source: list[AssetLocation] = field(default_factory=list)
    manifest_required: bool = False
    manifest_path: str | None = None
    coverage_categories: list[str] = field(default_factory=list)
    bsp_features: dict[str, Any] = field(default_factory=dict)

    def add(self, asset_type: str, location: AssetLocation) -> None:
        getattr(self, asset_type).append(location)

    @property
    def has_bsp(self) -> bool:
        return bool(self.bsp)

    @property
    def has_aas(self) -> bool:
        return bool(self.aas)

    @property
    def has_map_source(self) -> bool:
        return bool(self.map_source)

    @property
    def status(self) -> str:
        if self.has_bsp and self.has_aas:
            return "ready"
        if self.has_bsp and not self.has_aas:
            return "needs_conversion"
        if self.has_aas and not self.has_bsp:
            return "aas_without_bsp"
        if self.has_map_source:
            return "source_only"
        return "empty"

    @property
    def conversion_action(self) -> str:
        if self.status == "ready":
            return "none"
        if self.status == "needs_conversion":
            return "generate_aas_from_bsp"
        if self.status == "source_only":
            return "stage_or_build_bsp_before_aas_generation"
        if self.status == "aas_without_bsp":
            return "verify_source_bsp_or_package_pairing"
        return "investigate"

    def to_json(self) -> dict[str, Any]:
        return {
            "id": self.map_id,
            "status": self.status,
            "needs_conversion": self.status == "needs_conversion",
            "conversion_action": self.conversion_action,
            "manifest_required": self.manifest_required,
            "manifest_path": self.manifest_path,
            "coverage_categories": self.coverage_categories,
            "has_bsp": self.has_bsp,
            "has_aas": self.has_aas,
            "has_map_source": self.has_map_source,
            "bsp_locations": [location.to_json() for location in self.bsp],
            "aas_locations": [location.to_json() for location in self.aas],
            "map_source_locations": [location.to_json() for location in self.map_source],
            "bsp_features": self.bsp_features,
        }


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def display_path(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path.resolve())


def normalize_member_path(name: str) -> str:
    return name.replace("\\", "/").strip("/")


def map_id_from_name(name: str) -> str | None:
    normalized = normalize_member_path(name)
    suffix = PurePosixPath(normalized).suffix.lower()
    if suffix not in ASSET_EXTENSIONS:
        return None
    stem = PurePosixPath(normalized).stem.strip().lower()
    return stem or None


def asset_type_from_name(name: str) -> str | None:
    return ASSET_EXTENSIONS.get(PurePosixPath(normalize_member_path(name)).suffix.lower())


def add_asset(
    maps: dict[str, MapAssets],
    name: str,
    asset_type: str,
    location: AssetLocation,
) -> None:
    map_id = map_id_from_name(name)
    if map_id is None:
        return
    maps.setdefault(map_id, MapAssets(map_id)).add(asset_type, location)


def iter_zip_members(path: Path) -> tuple[list[tuple[str, int]], list[str]]:
    warnings: list[str] = []
    members: list[tuple[str, int]] = []
    try:
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if info.is_dir():
                    continue
                members.append((normalize_member_path(info.filename), info.file_size))
    except (OSError, zipfile.BadZipFile) as exc:
        warnings.append(f"unable to read zip archive {path}: {exc}")
    return members, warnings


def iter_pak_members(path: Path) -> tuple[list[tuple[str, int]], list[str]]:
    warnings: list[str] = []
    members: list[tuple[str, int]] = []
    try:
        archive_size = path.stat().st_size
    except OSError as exc:
        return members, [f"unable to read pak archive {path}: {exc}"]

    if archive_size < PAK_HEADER.size:
        return members, [f"invalid pak archive {path}: header is truncated"]

    try:
        with path.open("rb") as handle:
            header = handle.read(PAK_HEADER.size)
            ident, directory_offset, directory_size = PAK_HEADER.unpack(header)
            if ident != b"PACK":
                return members, [f"invalid pak archive {path}: missing PACK header"]
            if directory_offset < PAK_HEADER.size or directory_size < 0:
                return members, [f"invalid pak archive {path}: invalid directory bounds"]
            directory_end = directory_offset + directory_size
            if directory_end > archive_size or directory_size % PAK_ENTRY.size != 0:
                return members, [f"invalid pak archive {path}: truncated directory"]

            handle.seek(directory_offset)
            directory = handle.read(directory_size)
    except OSError as exc:
        return members, [f"unable to read pak directory {path}: {exc}"]

    if len(directory) != directory_size:
        return members, [f"invalid pak archive {path}: truncated directory"]

    for offset in range(0, directory_size, PAK_ENTRY.size):
        raw_name, member_offset, member_size = PAK_ENTRY.unpack_from(directory, offset)
        name = raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace")
        if not name:
            continue
        if member_offset < 0 or member_size < 0 or member_offset + member_size > archive_size:
            warnings.append(f"invalid pak member bounds in {path}: {name}")
            continue
        members.append((normalize_member_path(name), member_size))
    return members, warnings


def scan_archive(path: Path, root: Path, maps: dict[str, MapAssets]) -> list[str]:
    suffix = path.suffix.lower()
    if suffix in {".pkz", ".zip"}:
        members, warnings = iter_zip_members(path)
        container = "zip"
    elif suffix == ".pak":
        members, warnings = iter_pak_members(path)
        container = "pak"
    else:
        return []

    for member, size in members:
        asset_type = asset_type_from_name(member)
        if asset_type is None:
            continue
        add_asset(
            maps,
            member,
            asset_type,
            AssetLocation(
                container=container,
                path=display_path(path, root),
                member=member,
                size=size,
            ),
        )
    return warnings


def scan_loose_file(path: Path, root: Path, maps: dict[str, MapAssets]) -> None:
    asset_type = asset_type_from_name(path.name)
    if asset_type is None:
        return
    add_asset(
        maps,
        path.name,
        asset_type,
        AssetLocation(container="loose", path=display_path(path, root), size=path.stat().st_size),
    )


def scan_roots(root: Path, scan_roots: list[Path]) -> tuple[dict[str, MapAssets], list[str]]:
    maps: dict[str, MapAssets] = {}
    warnings: list[str] = []

    for scan_root in scan_roots:
        absolute_root = scan_root if scan_root.is_absolute() else root / scan_root
        if not absolute_root.exists():
            warnings.append(f"scan root missing: {display_path(absolute_root, root)}")
            continue
        if absolute_root.is_file():
            if absolute_root.suffix.lower() in ARCHIVE_EXTENSIONS:
                warnings.extend(scan_archive(absolute_root, root, maps))
            else:
                scan_loose_file(absolute_root, root, maps)
            continue
        for path in absolute_root.rglob("*"):
            if not path.is_file():
                continue
            suffix = path.suffix.lower()
            if suffix in ASSET_EXTENSIONS:
                scan_loose_file(path, root, maps)
            elif suffix in ARCHIVE_EXTENSIONS:
                warnings.extend(scan_archive(path, root, maps))

    return maps, warnings


def location_sort_key(location: AssetLocation) -> tuple[int, str, str]:
    return (
        LOCATION_CONTAINER_PRIORITY.get(location.container, 99),
        location.path,
        location.member or "",
    )


def primary_location(locations: list[AssetLocation]) -> AssetLocation | None:
    return sorted(locations, key=location_sort_key)[0] if locations else None


def validation_supported_bsp_location(assets: MapAssets) -> AssetLocation | None:
    for location in sorted(assets.bsp, key=location_sort_key):
        if location.container in Q2AAS_VALIDATION_SUPPORTED_CONTAINERS:
            return location
    return None


def resolve_location_path(root: Path, location: AssetLocation) -> Path:
    path = Path(location.path)
    return path if path.is_absolute() else root / path


def empty_feature_report(status: str, reason: str) -> dict[str, Any]:
    return {
        "status": status,
        "reason": reason,
        "features": {
            feature: {
                "present": False,
                "evidence": {},
            }
            for feature in REFERENCE_COVERAGE_FEATURES
        },
    }


def inspect_loose_bsp_features(root: Path, map_id: str, location: AssetLocation) -> dict[str, Any]:
    if location.container != "loose":
        report = empty_feature_report("not_inspected", "archive_member_not_extracted")
        report["location"] = location.to_json()
        return report
    if q2aas_validator is None:
        report = empty_feature_report("not_inspected", "q2aas_validator_unavailable")
        report["location"] = location.to_json()
        return report

    path = resolve_location_path(root, location)
    report: dict[str, Any] = {
        "status": "inspected",
        "location": location.to_json(),
        "format": "unknown",
        "valid_q2_bsp": False,
        "features": {},
    }
    try:
        bsp_info = q2aas_validator.inspect_q2_bsp(path)
        data = path.read_bytes()
        lumps = bsp_info.get("lumps", {})
        entities = (
            q2aas_validator.parse_entity_lump(data, lumps)
            if isinstance(lumps, dict)
            else []
        )
        entity_groups = q2aas_validator.group_entities(entities)
    except (OSError, struct.error, ValueError) as exc:
        failed = empty_feature_report("inspection_failed", str(exc))
        failed["location"] = location.to_json()
        failed["map_id"] = map_id
        return failed

    brush_contents = bsp_info.get("brush_contents", {})
    flag_counts = (
        brush_contents.get("flag_counts", {})
        if isinstance(brush_contents, dict)
        else {}
    )
    report.update(
        {
            "map_id": map_id,
            "format": bsp_info.get("format", "unknown"),
            "valid_q2_bsp": bool(bsp_info.get("valid_q2_bsp")),
        }
    )
    report["features"] = {
        "water": {
            "present": int(flag_counts.get("water", 0)) > 0,
            "evidence": {"brush_count": int(flag_counts.get("water", 0))},
        },
        "slime": {
            "present": int(flag_counts.get("slime", 0)) > 0,
            "evidence": {"brush_count": int(flag_counts.get("slime", 0))},
        },
        "lava": {
            "present": int(flag_counts.get("lava", 0)) > 0,
            "evidence": {"brush_count": int(flag_counts.get("lava", 0))},
        },
        "teleport": {
            "present": bool(entity_groups["teleports"]),
            "evidence": {"entity_count": len(entity_groups["teleports"])},
        },
        "elevator": {
            "present": bool(entity_groups["elevators"]),
            "evidence": {"entity_count": len(entity_groups["elevators"])},
        },
        "door": {
            "present": bool(entity_groups["doors"]),
            "evidence": {"entity_count": len(entity_groups["doors"])},
        },
    }
    return report


def apply_bsp_feature_reports(root: Path, maps: dict[str, MapAssets]) -> None:
    for map_id, assets in maps.items():
        location = primary_location(assets.bsp)
        if location is None:
            assets.bsp_features = empty_feature_report("not_inspected", "no_bsp_asset")
            continue
        assets.bsp_features = inspect_loose_bsp_features(root, map_id, location)


def load_manifest(path: Path) -> tuple[dict[str, Any] | None, list[str]]:
    if not path.exists():
        return None, [f"manifest missing: {path}"]
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        return None, [f"unable to read manifest {path}: {exc}"]
    if not isinstance(data, dict):
        return None, [f"manifest root is not an object: {path}"]
    return data, []


def manifest_string_list(value: Any) -> list[str]:
    if not isinstance(value, list):
        return []
    values: list[str] = []
    seen: set[str] = set()
    for item in value:
        if not isinstance(item, str) or not item:
            continue
        if item in seen:
            continue
        seen.add(item)
        values.append(item)
    return values


def manifest_bool(value: Any, default: bool) -> bool:
    return value if isinstance(value, bool) else default


def manifest_required_features(value: Any) -> list[str]:
    valid_features = set(REFERENCE_COVERAGE_FEATURES)
    return [
        feature
        for feature in manifest_string_list(value)
        if feature in valid_features
    ]


def empty_reference_feature_coverage() -> dict[str, Any]:
    return {
        feature: {
            "required_category_count": 0,
            "ready_category_count": 0,
            "missing_category_count": 0,
            "missing_category_ids": [],
            "candidate_map_ids": [],
            "ready_map_ids": [],
            "missing_map_ids": [],
        }
        for feature in REFERENCE_COVERAGE_FEATURES
    }


def inspect_candidate_required_features(
    map_id: str,
    candidate: dict[str, Any],
    required_features: list[str],
    maps: dict[str, MapAssets],
) -> dict[str, Any]:
    if not required_features:
        return {
            "feature_status": "not_required",
            "required_features": [],
            "missing_features": [],
            "observed_features": {},
        }

    assets = maps.get(map_id)
    bsp_features = assets.bsp_features if assets is not None else {}
    feature_reports = (
        bsp_features.get("features", {})
        if isinstance(bsp_features, dict)
        else {}
    )
    observed_features: dict[str, Any] = {}
    missing_features: list[str] = []
    for feature in required_features:
        feature_report = (
            feature_reports.get(feature, {})
            if isinstance(feature_reports, dict)
            else {}
        )
        observed_features[feature] = feature_report
        present = (
            bool(feature_report.get("present"))
            if isinstance(feature_report, dict)
            else False
        )
        if not present:
            missing_features.append(feature)

    if not candidate.get("available"):
        feature_status = "not_available"
    elif missing_features:
        feature_status = "missing_features"
    else:
        feature_status = "passed"

    return {
        "feature_status": feature_status,
        "feature_inspection_status": (
            bsp_features.get("status", "not_inspected")
            if isinstance(bsp_features, dict)
            else "not_inspected"
        ),
        "required_features": required_features,
        "missing_features": missing_features,
        "observed_features": observed_features,
    }


def build_reference_coverage_report(
    manifest: dict[str, Any],
    manifest_map_status: dict[str, dict[str, Any]],
    maps: dict[str, MapAssets],
) -> dict[str, Any]:
    raw_categories = manifest.get("reference_coverage", [])
    if not isinstance(raw_categories, list) or not raw_categories:
        return {
            "status": "not_configured",
            "category_count": 0,
            "category_status_counts": {"passed": 0, "incomplete": 0},
            "incomplete_category_count": 0,
            "missing_map_count": 0,
            "missing_category_map_count": 0,
            "candidate_absence_count": 0,
            "missing_optional_candidate_count": 0,
            "required_feature_category_count": 0,
            "feature_ready_category_count": 0,
            "feature_incomplete_category_count": 0,
            "missing_feature_category_count": 0,
            "missing_feature_map_count": 0,
            "unique_missing_map_ids": [],
            "feature_coverage": empty_reference_feature_coverage(),
            "missing_category_diagnostics": [],
            "categories": [],
            "incomplete_categories": [],
            "missing_maps": [],
            "feature_gap_maps": [],
            "strict_failed_categories": [],
            "strict_required_category_count": 0,
            "strict_gate": {
                "status": "not_configured",
                "passed": None,
                "failed_categories": [],
            },
        }

    categories = []
    incomplete_categories = []
    missing_maps = []
    candidate_absences = []
    missing_optional_candidate_count = 0
    feature_gap_maps = []
    missing_category_diagnostics = []
    feature_coverage = empty_reference_feature_coverage()
    required_feature_category_count = 0
    feature_ready_category_count = 0
    feature_incomplete_category_count = 0
    strict_failed_categories = []
    strict_required_category_count = 0
    for index, entry in enumerate(raw_categories):
        if not isinstance(entry, dict):
            continue
        category_id = entry.get("id")
        if not isinstance(category_id, str) or not category_id:
            category_id = f"unnamed-{index}"
        map_ids = manifest_string_list(entry.get("map_ids"))
        required_features = manifest_required_features(entry.get("required_features"))
        strict_required = manifest_bool(entry.get("strict_required"), True)
        minimum = entry.get("minimum_validated_maps", 1)
        if isinstance(minimum, bool) or not isinstance(minimum, int) or minimum < 1:
            minimum = 1

        candidate_maps = []
        available_count = 0
        validated_count = 0
        category_absences = []
        category_feature_gaps = []
        if not map_ids:
            absence = {
                "category": category_id,
                "id": None,
                "status": "no_candidate_declared",
                "path": None,
                "optional": True,
            }
            missing_maps.append(absence)
            candidate_absences.append(absence)
            category_absences.append(absence)
            missing_optional_candidate_count += 1
        for map_id in map_ids:
            map_status = manifest_map_status.get(map_id)
            if map_status is None:
                candidate = {
                    "id": map_id,
                    "status": "not_declared",
                    "required": False,
                }
            else:
                candidate = dict(map_status)
            if candidate.get("available"):
                available_count += 1
            else:
                absence = {
                    "category": category_id,
                    "id": map_id,
                    "status": candidate.get("status", "not_declared"),
                    "path": candidate.get("path"),
                    "optional": not bool(candidate.get("required", False)),
                }
                missing_maps.append(absence)
                candidate_absences.append(absence)
                category_absences.append(absence)
                if absence["optional"]:
                    missing_optional_candidate_count += 1
            feature_report = inspect_candidate_required_features(
                map_id,
                candidate,
                required_features,
                maps,
            )
            candidate.update(feature_report)
            if required_features:
                if candidate["feature_status"] == "passed":
                    validated_count += 1
                elif candidate.get("available"):
                    gap = {
                        "category": category_id,
                        "id": map_id,
                        "status": candidate["feature_status"],
                        "path": candidate.get("path"),
                        "required_features": required_features,
                        "missing_features": candidate["missing_features"],
                        "feature_inspection_status": candidate.get(
                            "feature_inspection_status",
                            "not_inspected",
                        ),
                    }
                    feature_gap_maps.append(gap)
                    category_feature_gaps.append(gap)
            elif candidate.get("available"):
                validated_count += 1
            candidate_maps.append(candidate)

        status = "passed" if validated_count >= minimum else "incomplete"
        if status == "incomplete":
            incomplete_categories.append(category_id)
        if required_features:
            required_feature_category_count += 1
            if status == "passed":
                feature_ready_category_count += 1
            else:
                feature_incomplete_category_count += 1
            for feature in required_features:
                ready_feature_map_ids = [
                    str(candidate["id"])
                    for candidate in candidate_maps
                    if candidate.get("available")
                    and feature not in candidate.get("missing_features", [])
                ]
                missing_feature_map_ids = [
                    str(candidate["id"])
                    for candidate in candidate_maps
                    if candidate.get("available")
                    and feature in candidate.get("missing_features", [])
                ]
                summary = feature_coverage[feature]
                summary["required_category_count"] += 1
                if status == "passed":
                    summary["ready_category_count"] += 1
                else:
                    summary["missing_category_count"] += 1
                    summary["missing_category_ids"].append(category_id)
                summary["candidate_map_ids"].extend(map_ids)
                summary["ready_map_ids"].extend(ready_feature_map_ids)
                summary["missing_map_ids"].extend(missing_feature_map_ids)
        if strict_required:
            strict_required_category_count += 1
        strict_gate_status = (
            "not_required"
            if not strict_required
            else "passed"
            if status == "passed"
            else "failed"
        )
        if strict_gate_status == "failed":
            strict_failed_categories.append(category_id)

        if status == "incomplete":
            reasons = []
            if not map_ids:
                reasons.append("no_candidate_declared")
            if category_absences:
                reasons.append("missing_candidate_assets")
            if category_feature_gaps:
                reasons.append("missing_required_features")
            if not reasons:
                reasons.append("insufficient_validated_maps")
            missing_category_diagnostics.append(
                {
                    "id": category_id,
                    "primary_reason": reasons[0],
                    "reasons": reasons,
                    "available_map_count": available_count,
                    "validated_map_count": validated_count,
                    "minimum_validated_maps": minimum,
                    "required_features": required_features,
                    "candidate_absence_count": len(category_absences),
                    "candidate_absences": category_absences,
                    "feature_gap_count": len(category_feature_gaps),
                    "feature_gaps": category_feature_gaps,
                }
            )

        categories.append({
            "id": category_id,
            "description": entry.get("description", "") if isinstance(entry.get("description"), str) else "",
            "status": status,
            "readiness": (
                "feature_ready"
                if required_features and status == "passed"
                else "feature_absent"
                if required_features
                else "candidate_ready"
                if status == "passed"
                else "candidate_absent"
            ),
            "available_map_count": available_count,
            "validated_map_count": validated_count,
            "feature_ready_map_count": validated_count if required_features else available_count,
            "minimum_validated_maps": minimum,
            "required_features": required_features,
            "strict_required": strict_required,
            "strict_gate": {
                "status": strict_gate_status,
                "passed": (
                    None
                    if strict_gate_status == "not_required"
                    else strict_gate_status == "passed"
                ),
            },
            "candidate_absence_count": len(category_absences),
            "candidate_absences": category_absences,
            "feature_gap_count": len(category_feature_gaps),
            "feature_gaps": category_feature_gaps,
            "candidate_maps": candidate_maps,
        })

    unique_missing_map_ids = sorted({
        str(entry["id"])
        for entry in missing_maps
        if entry.get("id")
    })
    for feature_summary in feature_coverage.values():
        for key in ("candidate_map_ids", "ready_map_ids", "missing_map_ids", "missing_category_ids"):
            feature_summary[key] = sorted(set(feature_summary[key]))

    return {
        "status": "incomplete" if incomplete_categories else "passed",
        "category_count": len(categories),
        "category_status_counts": {
            "passed": len(categories) - len(incomplete_categories),
            "incomplete": len(incomplete_categories),
        },
        "incomplete_category_count": len(incomplete_categories),
        "missing_map_count": len(unique_missing_map_ids),
        "missing_category_map_count": len(missing_maps),
        "candidate_absence_count": len(candidate_absences),
        "missing_optional_candidate_count": missing_optional_candidate_count,
        "required_feature_category_count": required_feature_category_count,
        "feature_ready_category_count": feature_ready_category_count,
        "feature_incomplete_category_count": feature_incomplete_category_count,
        "missing_feature_category_count": feature_incomplete_category_count,
        "missing_feature_map_count": len(feature_gap_maps),
        "unique_missing_map_ids": unique_missing_map_ids,
        "feature_coverage": feature_coverage,
        "missing_category_diagnostics": missing_category_diagnostics,
        "categories": categories,
        "incomplete_categories": incomplete_categories,
        "missing_maps": missing_maps,
        "feature_gap_maps": feature_gap_maps,
        "strict_failed_categories": strict_failed_categories,
        "strict_required_category_count": strict_required_category_count,
        "strict_gate": {
            "status": "failed" if strict_failed_categories else "passed",
            "passed": not strict_failed_categories,
            "failed_categories": strict_failed_categories,
        },
    }


def apply_manifest(maps: dict[str, MapAssets], manifest: dict[str, Any] | None) -> dict[str, Any]:
    if manifest is None:
        return {
            "path": None,
            "maps": [],
            "missing_required_maps": [],
            "pending_reference_maps": [],
            "pending_reference_status": [],
            "reference_coverage": {
                "status": "not_configured",
                "category_count": 0,
                "category_status_counts": {"passed": 0, "incomplete": 0},
                "incomplete_category_count": 0,
                "missing_map_count": 0,
                "missing_category_map_count": 0,
                "candidate_absence_count": 0,
                "missing_optional_candidate_count": 0,
                "required_feature_category_count": 0,
                "feature_ready_category_count": 0,
                "feature_incomplete_category_count": 0,
                "missing_feature_category_count": 0,
                "missing_feature_map_count": 0,
                "unique_missing_map_ids": [],
                "feature_coverage": empty_reference_feature_coverage(),
                "missing_category_diagnostics": [],
                "categories": [],
                "incomplete_categories": [],
                "missing_maps": [],
                "feature_gap_maps": [],
                "strict_failed_categories": [],
                "strict_required_category_count": 0,
                "strict_gate": {
                    "status": "not_configured",
                    "passed": None,
                    "failed_categories": [],
                },
            },
        }

    manifest_maps = []
    missing_required = []
    manifest_map_status: dict[str, dict[str, Any]] = {}
    for entry in manifest.get("maps", []):
        if not isinstance(entry, dict):
            continue
        map_id = str(entry.get("id", "")).strip().lower()
        if not map_id:
            continue
        required = bool(entry.get("required", False))
        manifest_path = entry.get("path")
        map_assets = maps.setdefault(map_id, MapAssets(map_id))
        map_assets.manifest_required = required
        if isinstance(manifest_path, str):
            map_assets.manifest_path = manifest_path
        coverage_categories = manifest_string_list(entry.get("coverage_categories"))
        map_assets.coverage_categories = coverage_categories
        found = map_assets.has_bsp or map_assets.has_aas or map_assets.has_map_source
        status = map_assets.status if found else "not_staged"
        manifest_maps.append(
            {
                "id": map_id,
                "path": manifest_path,
                "required": required,
                "found": found,
                "status": status,
                "coverage_categories": coverage_categories,
            }
        )
        if required and not manifest_maps[-1]["found"]:
            missing_required.append(map_id)
        manifest_map_status[map_id] = {
            "id": map_id,
            "status": status,
            "available": found,
            "path": manifest_path,
            "required": required,
            "coverage_categories": coverage_categories,
        }

    pending = [
        str(entry)
        for entry in manifest.get("pending_reference_maps", [])
        if isinstance(entry, str)
    ]
    pending_status = []
    found_ids = {
        map_id
        for map_id, assets in maps.items()
        if assets.has_bsp or assets.has_aas or assets.has_map_source
    }
    for label in pending:
        candidates = [
            token.lower()
            for token in re.findall(r"[A-Za-z][A-Za-z0-9_-]*", label)
            if token.lower() in found_ids
        ]
        pending_status.append(
            {
                "label": label,
                "status": "found" if candidates else "not_staged",
                "matched_map_ids": sorted(set(candidates)),
            }
        )

    return {
        "maps": manifest_maps,
        "missing_required_maps": missing_required,
        "pending_reference_maps": pending,
        "pending_reference_status": pending_status,
        "reference_coverage": build_reference_coverage_report(
            manifest,
            manifest_map_status,
            maps,
        ),
    }


def manifest_map_entries(manifest: dict[str, Any] | None) -> list[dict[str, Any]]:
    if manifest is None:
        return []
    raw_maps = manifest.get("maps", [])
    if not isinstance(raw_maps, list):
        return []
    return [entry for entry in raw_maps if isinstance(entry, dict)]


def manifest_reference_categories(manifest: dict[str, Any] | None) -> list[dict[str, Any]]:
    if manifest is None:
        return []
    raw_categories = manifest.get("reference_coverage", [])
    if not isinstance(raw_categories, list):
        return []
    return [entry for entry in raw_categories if isinstance(entry, dict)]


def manifest_map_id(entry: dict[str, Any]) -> str | None:
    value = entry.get("id")
    if not isinstance(value, str) or not value.strip():
        return None
    return value.strip().lower()


def manifest_minimum(value: Any, selected_count: int) -> int:
    minimum = value if isinstance(value, int) and not isinstance(value, bool) and value > 0 else 1
    return max(1, min(minimum, selected_count))


def append_note(existing: Any, addition: str) -> str:
    if isinstance(existing, str) and existing.strip():
        return f"{existing.strip()} {addition}"
    return addition


def manifest_source_from_location(location: AssetLocation) -> dict[str, str]:
    if location.container == "loose":
        return {"path": location.path}
    if location.container == "zip" and location.member:
        return {
            "archive": location.path,
            "archive_member": location.member,
        }
    return {}


def available_manifest_map_entry(
    map_id: str,
    source_entry: dict[str, Any],
    location: AssetLocation,
) -> dict[str, Any]:
    entry: dict[str, Any] = {
        "id": map_id,
        **manifest_source_from_location(location),
        "required": True,
    }
    for key in MANIFEST_MAP_CARRY_KEYS:
        if key in source_entry:
            entry[key] = source_entry[key]
    entry["notes"] = append_note(
        source_entry.get("notes", ""),
        (
            "Selected by aas_inventory for the available-reference validation subset; "
            "rerun the inventory before treating this focused manifest as current."
        ),
    )
    return entry


def feature_present(assets: MapAssets, feature: str) -> bool:
    features = assets.bsp_features.get("features", {})
    if not isinstance(features, dict):
        return False
    feature_report = features.get(feature, {})
    return isinstance(feature_report, dict) and bool(feature_report.get("present"))


def build_feature_candidate_map_ids(maps: dict[str, MapAssets]) -> dict[str, list[str]]:
    return {
        feature: sorted(
            map_id
            for map_id, assets in maps.items()
            if assets.has_bsp and feature_present(assets, feature)
        )
        for feature in REFERENCE_COVERAGE_FEATURES
    }


def build_reference_feature_suggestions(
    manifest: dict[str, Any] | None,
    maps: dict[str, MapAssets],
) -> list[dict[str, Any]]:
    suggestions: list[dict[str, Any]] = []
    for category in manifest_reference_categories(manifest):
        required_features = manifest_required_features(category.get("required_features"))
        if not required_features:
            continue
        candidate_ids = sorted(
            map_id
            for map_id, assets in maps.items()
            if assets.has_bsp and all(feature_present(assets, feature) for feature in required_features)
        )
        suggestions.append(
            {
                "id": category.get("id", ""),
                "required_features": required_features,
                "declared_map_ids": manifest_string_list(category.get("map_ids")),
                "discovered_candidate_map_ids": candidate_ids,
                "status": "candidate_found" if candidate_ids else "no_candidate_found",
            }
        )
    return suggestions


def build_available_reference_manifest_payload(
    manifest: dict[str, Any] | None,
    selected_entries: list[dict[str, Any]],
    selected_map_ids: list[str],
) -> dict[str, Any]:
    task_ids = (
        manifest_string_list(manifest.get("task_ids"))
        if manifest is not None
        else list(TASK_IDS)
    )
    if not task_ids:
        task_ids = list(TASK_IDS)

    selected_id_set = set(selected_map_ids)
    categories: list[dict[str, Any]] = []
    for category in manifest_reference_categories(manifest):
        category_map_ids = [
            map_id
            for map_id in manifest_string_list(category.get("map_ids"))
            if map_id.lower() in selected_id_set
        ]
        if not category_map_ids:
            continue
        selected_count = len(category_map_ids)
        category_entry: dict[str, Any] = {
            "id": category.get("id", "available_reference_subset"),
            "description": category.get("description", ""),
            "map_ids": category_map_ids,
            "minimum_validated_maps": manifest_minimum(
                category.get("minimum_validated_maps"),
                selected_count,
            ),
            "strict_required": True,
            "notes": append_note(
                category.get("notes", ""),
                (
                    "Focused available-reference subset generated from currently staged assets; "
                    "the canonical reference coverage remains in tools/q2aas/validation_manifest.json."
                ),
            ),
        }
        required_features = manifest_required_features(category.get("required_features"))
        if required_features:
            category_entry["required_features"] = required_features
        categories.append(category_entry)

    if selected_entries and not categories:
        categories.append(
            {
                "id": "available_reference_subset",
                "description": "Focused reference validation subset generated from staged BSP assets.",
                "map_ids": selected_map_ids,
                "minimum_validated_maps": 1,
                "strict_required": True,
            }
        )

    return {
        "schema": "worr-q2aas-validation-manifest-v1",
        "version": 1,
        "task_ids": task_ids,
        "maps": selected_entries,
        "reference_coverage": categories,
        "pending_reference_maps": [],
    }


def command_path(path: str | None) -> str:
    return (path or DEFAULT_AVAILABLE_REFERENCE_MANIFEST).replace("/", "\\")


def build_available_reference_commands(manifest_path: str | None) -> dict[str, str]:
    selected_manifest = command_path(manifest_path)
    return {
        "write_manifest": (
            "python -B tools\\aas_inventory\\inventory_aas_assets.py "
            f"--available-reference-manifest {selected_manifest}"
        ),
        "validate": (
            "python -B tools\\q2aas\\validate_worr_q2aas.py "
            f"--manifest {selected_manifest} "
            "--require-q2-bsp --require-reachability --require-clean-bsp-lumps "
            "--require-spawn-coverage --require-item-coverage "
            "--require-high-value-reachability --write-aas-metadata "
            "--require-reference-coverage "
            "--report-json .tmp\\q2aas\\available-reference-validation-report.json"
        ),
        "validate_and_stage": (
            "python -B tools\\q2aas\\validate_worr_q2aas.py "
            f"--manifest {selected_manifest} "
            "--require-q2-bsp --require-reachability --require-clean-bsp-lumps "
            "--require-spawn-coverage --require-item-coverage "
            "--require-high-value-reachability --write-aas-metadata "
            "--require-reference-coverage --stage-aas --stage-aas-dir .install\\basew\\maps "
            "--report-json .tmp\\q2aas\\available-reference-stage-report.json"
        ),
    }


def build_available_reference_validation(
    root: Path,
    maps: dict[str, MapAssets],
    manifest: dict[str, Any] | None,
    manifest_output_path: str | None,
) -> dict[str, Any]:
    declared_ids: list[str] = []
    selected_entries: list[dict[str, Any]] = []
    selected_map_ids: list[str] = []
    runtime_ready_map_ids: list[str] = []
    needs_aas_map_ids: list[str] = []
    omitted_declared_maps: list[dict[str, Any]] = []

    for entry in manifest_map_entries(manifest):
        map_id = manifest_map_id(entry)
        if map_id is None:
            continue
        declared_ids.append(map_id)
        assets = maps.get(map_id)
        if assets is None or not assets.has_bsp:
            omitted_declared_maps.append(
                {
                    "id": map_id,
                    "status": "not_staged",
                    "reason": "no_bsp_asset",
                }
            )
            continue
        location = validation_supported_bsp_location(assets)
        if location is None:
            omitted_declared_maps.append(
                {
                    "id": map_id,
                    "status": assets.status,
                    "reason": "no_q2aas_manifest_supported_bsp_location",
                    "bsp_locations": [item.to_json() for item in assets.bsp],
                }
            )
            continue
        selected_entries.append(available_manifest_map_entry(map_id, entry, location))
        selected_map_ids.append(map_id)
        if assets.has_aas:
            runtime_ready_map_ids.append(map_id)
        else:
            needs_aas_map_ids.append(map_id)

    unmanifested_bsp_map_ids = sorted(
        map_id
        for map_id, assets in maps.items()
        if assets.has_bsp and map_id not in set(declared_ids)
    )
    feature_candidate_map_ids = build_feature_candidate_map_ids(maps)
    reference_feature_suggestions = build_reference_feature_suggestions(manifest, maps)
    manifest_payload = build_available_reference_manifest_payload(
        manifest,
        selected_entries,
        selected_map_ids,
    )
    status = "ready" if selected_map_ids else "no_available_manifest_bsp"
    runtime_status = "ready" if runtime_ready_map_ids else "needs_aas_generation"
    if not selected_map_ids:
        runtime_status = "not_ready"

    return {
        "schema": REFERENCE_SELECTION_SCHEMA,
        "status": status,
        "selection_policy": (
            "Select manifest-declared maps that have a BSP asset in the scanned roots; "
            "prefer loose files, accept zip/pkz members, and omit pak-only BSPs because "
            "validate_worr_q2aas manifest extraction is zip/pkz based."
        ),
        "focused_manifest_path": manifest_output_path or DEFAULT_AVAILABLE_REFERENCE_MANIFEST,
        "selected_map_ids": selected_map_ids,
        "runtime_status": runtime_status,
        "runtime_ready_map_ids": runtime_ready_map_ids,
        "needs_aas_map_ids": needs_aas_map_ids,
        "omitted_declared_maps": omitted_declared_maps,
        "unmanifested_bsp_map_ids": unmanifested_bsp_map_ids,
        "feature_candidate_map_ids": feature_candidate_map_ids,
        "reference_feature_suggestions": reference_feature_suggestions,
        "summary": {
            "selected_map_count": len(selected_map_ids),
            "runtime_ready_map_count": len(runtime_ready_map_ids),
            "needs_aas_map_count": len(needs_aas_map_ids),
            "omitted_declared_map_count": len(omitted_declared_maps),
            "unmanifested_bsp_map_count": len(unmanifested_bsp_map_ids),
            "feature_candidate_counts": {
                feature: len(map_ids)
                for feature, map_ids in feature_candidate_map_ids.items()
            },
            "reference_feature_suggestion_count": len(reference_feature_suggestions),
            "reference_feature_gap_count": sum(
                1
                for suggestion in reference_feature_suggestions
                if suggestion.get("status") == "no_candidate_found"
            ),
        },
        "manifest_summary": {
            "map_count": len(selected_entries),
            "reference_coverage_count": len(manifest_payload["reference_coverage"]),
        },
        "manifest_payload": manifest_payload,
        "commands": build_available_reference_commands(manifest_output_path),
    }


def inspect_reference_paths(paths: list[Path]) -> list[dict[str, Any]]:
    references = []
    for path in paths:
        exists = path.exists()
        references.append(
            {
                "path": str(path),
                "exists": exists,
                "type": "directory" if exists and path.is_dir() else "file" if exists else "missing",
            }
        )
    return references


def summarize(maps: dict[str, MapAssets]) -> dict[str, int]:
    summary = {
        "total_maps": len(maps),
        "ready": 0,
        "needs_conversion": 0,
        "source_only": 0,
        "aas_without_bsp": 0,
        "manifest_required": 0,
    }
    for assets in maps.values():
        if assets.status in summary:
            summary[assets.status] += 1
        if assets.manifest_required:
            summary["manifest_required"] += 1
    return summary


def build_inventory(
    root: Path,
    scan_root_args: list[str] | None,
    manifest_arg: str | None,
    reference_path_args: list[str],
    available_reference_manifest_arg: str | None = None,
) -> dict[str, Any]:
    scan_root_values = scan_root_args if scan_root_args else list(DEFAULT_SCAN_ROOTS)
    scan_paths = [Path(value) for value in scan_root_values]
    maps, warnings = scan_roots(root, scan_paths)
    apply_bsp_feature_reports(root, maps)

    manifest_path = root / manifest_arg if manifest_arg else None
    manifest: dict[str, Any] | None = None
    manifest_report: dict[str, Any]
    if manifest_path is not None:
        manifest, manifest_warnings = load_manifest(manifest_path)
        warnings.extend(manifest_warnings)
        manifest_report = apply_manifest(maps, manifest)
        manifest_report["path"] = display_path(manifest_path, root)
    else:
        manifest_report = apply_manifest(maps, None)

    apply_bsp_feature_reports(root, maps)
    available_reference_validation = build_available_reference_validation(
        root,
        maps,
        manifest,
        available_reference_manifest_arg,
    )
    reference_paths = [Path(value) for value in reference_path_args]
    return {
        "schema": REPORT_SCHEMA,
        "task_ids": list(TASK_IDS),
        "scan_roots": [display_path(path if path.is_absolute() else root / path, root) for path in scan_paths],
        "summary": summarize(maps),
        "maps": [assets.to_json() for assets in sorted(maps.values(), key=lambda item: item.map_id)],
        "manifest": manifest_report,
        "available_reference_validation": available_reference_validation,
        "reference_sources": inspect_reference_paths(reference_paths),
        "warnings": warnings,
    }


def write_available_reference_manifest(path: Path, root: Path, report: dict[str, Any]) -> None:
    validation = report.get("available_reference_validation", {})
    if not isinstance(validation, dict):
        raise ValueError("available_reference_validation report is missing")
    payload = validation.get("manifest_payload")
    if not isinstance(payload, dict):
        raise ValueError("available reference manifest payload is missing")
    absolute = path if path.is_absolute() else root / path
    absolute.parent.mkdir(parents=True, exist_ok=True)
    absolute.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def write_report(path: Path, root: Path, report: dict[str, Any]) -> None:
    absolute = path if path.is_absolute() else root / path
    absolute.parent.mkdir(parents=True, exist_ok=True)
    absolute.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def print_text_report(report: dict[str, Any], report_json: str | None) -> None:
    summary = report["summary"]
    print(
        "[aas-inventory] summary: "
        f"maps={summary['total_maps']} ready={summary['ready']} "
        f"needs_conversion={summary['needs_conversion']} "
        f"source_only={summary['source_only']} "
        f"aas_without_bsp={summary['aas_without_bsp']} "
        f"manifest_required={summary['manifest_required']}"
    )
    for status in ("ready", "needs_conversion", "source_only", "aas_without_bsp"):
        entries = [entry for entry in report["maps"] if entry["status"] == status]
        if not entries:
            print(f"[aas-inventory] {status}: none")
            continue
        print(f"[aas-inventory] {status}:")
        for entry in entries:
            print(
                "  - "
                f"{entry['id']}: bsp={len(entry['bsp_locations'])} "
                f"aas={len(entry['aas_locations'])} "
                f"map_source={len(entry['map_source_locations'])} "
                f"action={entry['conversion_action']}"
            )

    missing_required = report["manifest"]["missing_required_maps"]
    if missing_required:
        print(f"[aas-inventory] missing required manifest maps: {', '.join(missing_required)}")
    else:
        print("[aas-inventory] missing required manifest maps: none")

    pending = report["manifest"]["pending_reference_status"]
    not_staged = [entry["label"] for entry in pending if entry["status"] == "not_staged"]
    if not_staged:
        print("[aas-inventory] pending reference maps not staged:")
        for label in not_staged:
            print(f"  - {label}")
    elif pending:
        print("[aas-inventory] pending reference maps not staged: none")

    reference_coverage = report["manifest"].get("reference_coverage", {})
    if reference_coverage.get("status") == "incomplete":
        print("[aas-inventory] reference coverage incomplete:")
        for category in reference_coverage.get("categories", []):
            if category.get("status") != "incomplete":
                continue
            print(
                "  - "
                f"{category['id']}: available={category['available_map_count']} "
                f"minimum={category['minimum_validated_maps']}"
            )
            features = category.get("required_features", [])
            if features:
                print(f"    required_features={', '.join(features)}")
                print(
                    "    "
                    f"feature_ready={category.get('feature_ready_map_count', 0)} "
                    f"minimum={category.get('minimum_validated_maps', 1)}"
                )
            for absence in category.get("candidate_absences", []):
                if absence.get("status") == "no_candidate_declared":
                    print("    no optional candidate map is declared yet")
                    continue
                path = absence.get("path")
                suffix = f" ({path})" if path else ""
                optional = "optional " if absence.get("optional") else ""
                print(f"    {absence['id']}: missing {optional}candidate {absence['status']}{suffix}")
            for gap in category.get("feature_gaps", []):
                missing_features = ", ".join(str(feature) for feature in gap.get("missing_features", []))
                path = gap.get("path")
                suffix = f" ({path})" if path else ""
                print(
                    "    "
                    f"{gap['id']}: missing required features "
                    f"{missing_features or 'unknown'}{suffix}"
                )
    elif reference_coverage.get("status") == "passed":
        print("[aas-inventory] reference coverage: all configured categories have assets")

    available = report.get("available_reference_validation", {})
    if isinstance(available, dict):
        selected = available.get("selected_map_ids", [])
        runtime_ready = available.get("runtime_ready_map_ids", [])
        needs_aas = available.get("needs_aas_map_ids", [])
        if selected:
            print(
                "[aas-inventory] available reference validation maps: "
                + ", ".join(str(map_id) for map_id in selected)
            )
            if runtime_ready:
                print(
                    "[aas-inventory] runtime-ready BSP+AAS maps: "
                    + ", ".join(str(map_id) for map_id in runtime_ready)
                )
            if needs_aas:
                print(
                    "[aas-inventory] BSP maps needing generated AAS: "
                    + ", ".join(str(map_id) for map_id in needs_aas)
                )
            if available.get("focused_manifest_path"):
                print(
                    "[aas-inventory] focused manifest path: "
                    f"{available['focused_manifest_path']}"
                )
            commands = available.get("commands", {})
            if isinstance(commands, dict) and commands.get("validate"):
                print(f"[aas-inventory] focused validation command: {commands['validate']}")
        else:
            print("[aas-inventory] available reference validation maps: none")

        feature_candidates = available.get("feature_candidate_map_ids", {})
        if isinstance(feature_candidates, dict):
            present_features = [
                f"{feature}={','.join(str(map_id) for map_id in map_ids)}"
                for feature, map_ids in feature_candidates.items()
                if isinstance(map_ids, list) and map_ids
            ]
            if present_features:
                print(
                    "[aas-inventory] discovered BSP feature candidates: "
                    + "; ".join(present_features)
                )

    missing_refs = [entry["path"] for entry in report["reference_sources"] if not entry["exists"]]
    if missing_refs:
        print("[aas-inventory] missing reference source paths:")
        for path in missing_refs:
            print(f"  - {path}")
    else:
        print("[aas-inventory] reference source paths: all present")

    for warning in report["warnings"]:
        print(f"[aas-inventory] warning: {warning}", file=sys.stderr)
    if report_json:
        print(f"[aas-inventory] report: {report_json}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Inventory loose and packaged WORR BSP/AAS map assets."
    )
    parser.add_argument("--root", default=str(repo_root()), help="Repository root.")
    parser.add_argument(
        "--scan-root",
        action="append",
        help="Root, file, or archive to scan. Defaults to assets, .install, and refs.",
    )
    parser.add_argument(
        "--manifest",
        default=DEFAULT_MANIFEST,
        help="q2aas validation manifest to cross-reference, or empty to skip.",
    )
    parser.add_argument(
        "--reference-path",
        action="append",
        default=list(DEFAULT_REFERENCE_PATHS),
        help="External reference source path to record in the report.",
    )
    parser.add_argument(
        "--report-json",
        default=DEFAULT_REPORT_JSON,
        help="JSON report path. Use an empty value to skip writing a report.",
    )
    parser.add_argument(
        "--available-reference-manifest",
        default=None,
        help=(
            "Write a focused q2aas validation manifest containing only manifest-declared "
            "reference BSPs discovered in the scanned roots. Suggested path: "
            f"{DEFAULT_AVAILABLE_REFERENCE_MANIFEST}."
        ),
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="stdout format.",
    )
    parser.add_argument(
        "--fail-on-needs-conversion",
        action="store_true",
        help="Exit non-zero if any BSP-backed map lacks an AAS.",
    )
    parser.add_argument(
        "--fail-on-missing-required-manifest",
        action="store_true",
        help="Exit non-zero if the manifest marks a map required but no matching asset is found.",
    )
    parser.add_argument(
        "--fail-on-incomplete-reference-coverage",
        action="store_true",
        help="Exit non-zero if manifest reference coverage categories lack staged map assets.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = Path(args.root).resolve()
    manifest = args.manifest if args.manifest else None
    report_json = args.report_json if args.report_json else None
    available_reference_manifest = (
        args.available_reference_manifest if args.available_reference_manifest else None
    )
    report = build_inventory(
        root,
        args.scan_root,
        manifest,
        args.reference_path,
        available_reference_manifest,
    )

    if available_reference_manifest:
        write_available_reference_manifest(Path(available_reference_manifest), root, report)

    if report_json:
        write_report(Path(report_json), root, report)

    if args.format == "json":
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_text_report(report, report_json)

    failed = False
    if args.fail_on_needs_conversion and report["summary"]["needs_conversion"]:
        failed = True
    if args.fail_on_missing_required_manifest and report["manifest"]["missing_required_maps"]:
        failed = True
    if (
        args.fail_on_incomplete_reference_coverage
        and report["manifest"]["reference_coverage"]["status"] == "incomplete"
    ):
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
