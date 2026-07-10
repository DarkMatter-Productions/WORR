#!/usr/bin/env python3
"""Validate the WORR RmlUi smoke manifest and present route documents."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath
from typing import Any
from urllib.parse import unquote, urlsplit
from xml.etree import ElementTree


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
MIGRATION_PHASE_FIELD = "migration_phase"
MIGRATION_PHASE_REQUIRED_FIELD = "migration_phase_required"
MIGRATION_PHASES = (
    "starter",
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)


@dataclass
class ValidationStats:
    rml_files_parsed: int = 0
    href_imports_checked: int = 0


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("manifest root must be a JSON object")
    return data


def route_label(route: dict[str, Any]) -> str:
    return str(route.get("id", "<missing id>"))


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.relative_to(repo_root).as_posix()
    except ValueError:
        return str(path)


def is_within_repo(path: Path, repo_root: Path) -> bool:
    try:
        path.relative_to(repo_root)
    except ValueError:
        return False
    return True


def tag_name(tag: str) -> str:
    if "}" in tag:
        return tag.rsplit("}", 1)[1]
    return tag


def is_windows_drive_href(href: str) -> bool:
    return len(href) >= 3 and href[0].isalpha() and href[1] == ":" and href[2] in ("/", "\\")


def local_href_path(href: str) -> str | None:
    stripped = href.strip()
    if not stripped or stripped.startswith("#") or stripped.startswith("{{"):
        return None

    if is_windows_drive_href(stripped):
        return stripped.split("#", 1)[0].split("?", 1)[0]

    parsed = urlsplit(stripped)
    if parsed.netloc:
        return None
    if parsed.scheme and parsed.scheme != "file":
        return None

    path = unquote(parsed.path)
    if not path or path.startswith("{{"):
        return None
    return path


def validate_manifest_shape(data: dict[str, Any], errors: list[str]) -> None:
    for field in ("description", "created", "roadmap"):
        if field in data and not isinstance(data[field], str):
            errors.append(f"manifest field {field!r} must be a string")

    if (
        MIGRATION_PHASE_REQUIRED_FIELD in data
        and not isinstance(data[MIGRATION_PHASE_REQUIRED_FIELD], bool)
    ):
        errors.append(
            f"manifest field {MIGRATION_PHASE_REQUIRED_FIELD!r} must be a boolean when present"
        )

    tasks = data.get("tasks")
    if tasks is None:
        return
    if not isinstance(tasks, list):
        errors.append("manifest field 'tasks' must be a list when present")
        return
    for index, task in enumerate(tasks):
        if not isinstance(task, str) or not task:
            errors.append(f"manifest task at index {index} must be a non-empty string")


def validate_rml_file(
    path: Path,
    repo_root: Path,
    route_id: str,
    errors: list[str],
    stats: ValidationStats,
    seen_rml: set[Path],
    *,
    require_rml_root: bool,
) -> None:
    resolved_path = path.resolve(strict=False)
    if resolved_path in seen_rml:
        return
    seen_rml.add(resolved_path)

    try:
        root = ElementTree.parse(path).getroot()
    except ElementTree.ParseError as exc:
        errors.append(f"route {route_id!r} has malformed RML {display_path(path, repo_root)}: {exc}")
        return
    except OSError as exc:
        errors.append(f"route {route_id!r} cannot read RML {display_path(path, repo_root)}: {exc}")
        return

    stats.rml_files_parsed += 1
    root_tag = tag_name(root.tag)
    if require_rml_root and root_tag != "rml":
        errors.append(
            f"route {route_id!r} document {display_path(path, repo_root)} "
            f"root must be <rml>, found <{root_tag}>"
        )

    for element in root.iter():
        if tag_name(element.tag) != "link":
            continue

        href = element.attrib.get("href")
        if href is None or not href.strip():
            errors.append(f"route {route_id!r} link in {display_path(path, repo_root)} is missing href")
            continue

        href_path = local_href_path(href)
        if href_path is None:
            continue

        stats.href_imports_checked += 1
        if Path(href_path).is_absolute() or PureWindowsPath(href_path).is_absolute():
            errors.append(
                f"route {route_id!r} local href import must be relative in "
                f"{display_path(path, repo_root)}: {href}"
            )
            continue

        import_path = (path.parent / href_path).resolve(strict=False)
        if not is_within_repo(import_path, repo_root):
            errors.append(
                f"route {route_id!r} local href import escapes repo in "
                f"{display_path(path, repo_root)}: {href}"
            )
            continue
        if not import_path.exists():
            errors.append(
                f"route {route_id!r} missing local href import {href} "
                f"referenced by {display_path(path, repo_root)}"
            )
            continue
        if import_path.is_dir():
            errors.append(
                f"route {route_id!r} local href import is a directory in "
                f"{display_path(path, repo_root)}: {href}"
            )
            continue

        if import_path.suffix.lower() == ".rml":
            validate_rml_file(
                import_path,
                repo_root,
                route_id,
                errors,
                stats,
                seen_rml,
                require_rml_root=False,
            )


def validate_manifest(data: dict[str, Any], repo_root: Path, *, verbose: bool = False) -> int:
    errors: list[str] = []
    warnings: list[str] = []
    present: list[dict[str, Any]] = []
    pending: list[dict[str, Any]] = []
    stats = ValidationStats()

    schema = data.get("schema")
    if schema != EXPECTED_SCHEMA:
        errors.append(f"unexpected schema {schema!r}; expected {EXPECTED_SCHEMA!r}")
    validate_manifest_shape(data, errors)

    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append("manifest field 'routes' must be a list")
        routes = []

    seen_ids: set[str] = set()
    duplicate_ids: set[str] = set()
    wave_counts: Counter[str] = Counter()
    migration_phase_counts: Counter[str] = Counter()
    migration_phase_required = data.get(MIGRATION_PHASE_REQUIRED_FIELD, False) is True
    required_present = 0

    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            errors.append(f"route at index {index} must be an object")
            continue

        route_id = route.get("id")
        document = route.get("document")
        wave_value = route.get("wave", "unknown")
        required_value = route.get("required_now")

        if not isinstance(route_id, str) or not route_id:
            errors.append(f"route at index {index} is missing a non-empty id")
            continue
        if route_id in seen_ids:
            duplicate_ids.add(route_id)
        seen_ids.add(route_id)

        if not isinstance(wave_value, str) or not wave_value:
            errors.append(f"route {route_id!r} field 'wave' must be a non-empty string when present")
            wave = "unknown"
        else:
            wave = wave_value
        wave_counts[wave] += 1

        for field in ("owner", "status"):
            if field in route and (not isinstance(route[field], str) or not route[field]):
                errors.append(f"route {route_id!r} field {field!r} must be a non-empty string when present")

        migration_phase_value = route.get(MIGRATION_PHASE_FIELD)
        if migration_phase_value is None:
            if migration_phase_required:
                errors.append(
                    f"route {route_id!r} field {MIGRATION_PHASE_FIELD!r} is required "
                    f"when {MIGRATION_PHASE_REQUIRED_FIELD!r} is true"
                )
        elif not isinstance(migration_phase_value, str) or migration_phase_value not in MIGRATION_PHASES:
            allowed = ", ".join(MIGRATION_PHASES)
            errors.append(
                f"route {route_id!r} field {MIGRATION_PHASE_FIELD!r} must be one of "
                f"{allowed}; got {migration_phase_value!r}"
            )
        else:
            migration_phase_counts[migration_phase_value] += 1

        if not isinstance(required_value, bool):
            errors.append(f"route {route_id!r} field 'required_now' must be a boolean")
            required_now = False
        else:
            required_now = required_value

        if not isinstance(document, str) or not document:
            errors.append(f"route {route_id!r} is missing a non-empty document path")
            continue
        if Path(document).is_absolute():
            errors.append(f"route {route_id!r} document path must be repo-relative: {document}")
            continue
        if Path(document).suffix.lower() != ".rml":
            errors.append(f"route {route_id!r} document path must point to an .rml file: {document}")
            continue

        document_path = (repo_root / document).resolve(strict=False)
        if not is_within_repo(document_path, repo_root):
            errors.append(f"route {route_id!r} document path escapes repo: {document}")
            continue
        if document_path.exists():
            present.append(route)
            if required_now:
                required_present += 1
            validate_rml_file(
                document_path,
                repo_root,
                route_id,
                errors,
                stats,
                seen_rml=set(),
                require_rml_root=True,
            )
        elif required_now:
            errors.append(f"required route {route_id!r} is missing document {document}")
        else:
            pending.append(route)

    for route_id in sorted(duplicate_ids):
        errors.append(f"duplicate route id {route_id!r}")

    required_count = sum(1 for route in routes if isinstance(route, dict) and route.get("required_now") is True)
    print(f"Manifest: {data.get('schema', '<missing schema>')}")
    print(
        f"Routes: {len(routes)} total, {required_count} required, "
        f"{len(present)} present, {len(pending)} pending"
    )
    if wave_counts:
        print("Waves: " + ", ".join(f"{wave}={wave_counts[wave]}" for wave in sorted(wave_counts)))
    if migration_phase_counts:
        phase_summary = ", ".join(
            f"{phase}={migration_phase_counts[phase]}"
            for phase in MIGRATION_PHASES
            if migration_phase_counts[phase]
        )
        print(f"Migration phases: {phase_summary}")
    print(f"Required documents present: {required_present}/{required_count}")
    print(f"RML parsed: {stats.rml_files_parsed} files, href imports checked: {stats.href_imports_checked}")

    if pending and verbose:
        print("\nPending routes:")
        for route in pending:
            print(f"  - {route_label(route)} -> {route.get('document')} ({route.get('owner')})")

    if warnings:
        print("\nWarnings:")
        for warning in warnings:
            print(f"  - {warning}")

    if errors:
        print("\nErrors:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("\nResult: RmlUi manifest check passed.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("rmlui_manifest.json"),
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve manifest document paths.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print pending route details in addition to the concise summary.",
    )
    args = parser.parse_args(argv)

    manifest_path = args.manifest.resolve()
    repo_root = args.repo_root.resolve()

    try:
        data = load_manifest(manifest_path)
        return validate_manifest(data, repo_root, verbose=args.verbose)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Failed to load manifest {manifest_path}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
