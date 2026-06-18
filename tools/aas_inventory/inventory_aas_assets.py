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
DEFAULT_REFERENCE_PATHS = (
    r"E:\_SOURCE\_CODE\Quake-III-Arena-master",
    r"E:\_SOURCE\_CODE\Quake-III-Arena-master\code\bspc",
    r"E:\_SOURCE\_CODE\Quake3e-master",
    r"E:\_SOURCE\_CODE\baseq3a-master",
)

PAK_HEADER = struct.Struct("<4sii")
PAK_ENTRY = struct.Struct("<56sii")


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


def build_reference_coverage_report(
    manifest: dict[str, Any],
    manifest_map_status: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    raw_categories = manifest.get("reference_coverage", [])
    if not isinstance(raw_categories, list) or not raw_categories:
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

    categories = []
    incomplete_categories = []
    missing_maps = []
    for index, entry in enumerate(raw_categories):
        if not isinstance(entry, dict):
            continue
        category_id = entry.get("id")
        if not isinstance(category_id, str) or not category_id:
            category_id = f"unnamed-{index}"
        map_ids = manifest_string_list(entry.get("map_ids"))
        minimum = entry.get("minimum_validated_maps", 1)
        if isinstance(minimum, bool) or not isinstance(minimum, int) or minimum < 1:
            minimum = 1

        candidate_maps = []
        available_count = 0
        for map_id in map_ids:
            map_status = manifest_map_status.get(map_id)
            if map_status is None:
                candidate = {
                    "id": map_id,
                    "status": "not_declared",
                }
            else:
                candidate = dict(map_status)
            if candidate.get("available"):
                available_count += 1
            else:
                missing_maps.append({
                    "category": category_id,
                    "id": map_id,
                    "status": candidate.get("status", "not_declared"),
                    "path": candidate.get("path"),
                })
            candidate_maps.append(candidate)

        status = "passed" if available_count >= minimum else "incomplete"
        if status == "incomplete":
            incomplete_categories.append(category_id)

        categories.append({
            "id": category_id,
            "description": entry.get("description", "") if isinstance(entry.get("description"), str) else "",
            "status": status,
            "available_map_count": available_count,
            "minimum_validated_maps": minimum,
            "candidate_maps": candidate_maps,
        })

    unique_missing_map_ids = sorted({
        str(entry["id"])
        for entry in missing_maps
        if entry.get("id")
    })

    return {
        "status": "incomplete" if incomplete_categories else "passed",
        "category_count": len(categories),
        "incomplete_category_count": len(incomplete_categories),
        "missing_map_count": len(unique_missing_map_ids),
        "missing_category_map_count": len(missing_maps),
        "unique_missing_map_ids": unique_missing_map_ids,
        "categories": categories,
        "incomplete_categories": incomplete_categories,
        "missing_maps": missing_maps,
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
                "incomplete_category_count": 0,
                "missing_map_count": 0,
                "missing_category_map_count": 0,
                "unique_missing_map_ids": [],
                "categories": [],
                "incomplete_categories": [],
                "missing_maps": [],
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
    found_ids = set(maps)
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
        "reference_coverage": build_reference_coverage_report(manifest, manifest_map_status),
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
) -> dict[str, Any]:
    scan_root_values = scan_root_args if scan_root_args else list(DEFAULT_SCAN_ROOTS)
    scan_paths = [Path(value) for value in scan_root_values]
    maps, warnings = scan_roots(root, scan_paths)

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

    reference_paths = [Path(value) for value in reference_path_args]
    return {
        "schema": REPORT_SCHEMA,
        "task_ids": list(TASK_IDS),
        "scan_roots": [display_path(path if path.is_absolute() else root / path, root) for path in scan_paths],
        "summary": summarize(maps),
        "maps": [assets.to_json() for assets in sorted(maps.values(), key=lambda item: item.map_id)],
        "manifest": manifest_report,
        "reference_sources": inspect_reference_paths(reference_paths),
        "warnings": warnings,
    }


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
            for candidate in category.get("candidate_maps", []):
                if candidate.get("available"):
                    continue
                path = candidate.get("path")
                suffix = f" ({path})" if path else ""
                print(f"    {candidate['id']}: {candidate['status']}{suffix}")
    elif reference_coverage.get("status") == "passed":
        print("[aas-inventory] reference coverage: all configured categories have assets")

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
    report = build_inventory(root, args.scan_root, manifest, args.reference_path)

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
