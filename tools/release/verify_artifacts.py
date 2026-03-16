#!/usr/bin/env python3

from __future__ import annotations

import argparse
import fnmatch
import json
import pathlib
import sys
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.release.targets import TARGETS, get_target


def choose_targets(platform_ids: list[str]) -> list[dict[str, Any]]:
    if platform_ids:
        return [get_target(platform_id) for platform_id in platform_ids]
    return list(TARGETS)


def require_asset(artifacts_root: pathlib.Path, name: str) -> pathlib.Path:
    direct = artifacts_root / name
    if direct.is_file():
        return direct

    for match in artifacts_root.rglob(name):
        if match.is_file():
            return match

    raise FileNotFoundError(name)


def load_manifest(artifacts_root: pathlib.Path, name: str) -> dict[str, Any]:
    manifest_path = require_asset(artifacts_root, name)
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def load_metadata(artifacts_root: pathlib.Path, platform_id: str) -> dict[str, Any]:
    metadata_name = f"metadata-{platform_id}.json"
    metadata_path = require_asset(artifacts_root, metadata_name)
    return json.loads(metadata_path.read_text(encoding="utf-8"))


def matching_paths(paths: list[str], pattern: str) -> list[str]:
    return [path for path in paths if fnmatch.fnmatch(path, pattern)]


def validate_manifest(
    failures: list[str],
    target: dict[str, Any],
    role: str,
    manifest: dict[str, Any],
) -> None:
    config = target[role]
    platform_id = target["platform_id"]
    manifest_name = config["manifest_name"]
    package_name = config["package_name"]

    package = manifest.get("package", {})
    if package.get("name") != package_name:
        failures.append(
            f"{platform_id} {role}: {manifest_name} package name mismatch "
            f"({package.get('name')} != {package_name})"
        )

    files = manifest.get("files", [])
    if not isinstance(files, list):
        failures.append(f"{platform_id} {role}: {manifest_name} has invalid files payload")
        return

    rel_paths = sorted(
        entry.get("path", "")
        for entry in files
        if isinstance(entry, dict) and isinstance(entry.get("path"), str)
    )

    for pattern in config.get("required_paths", []):
        if not matching_paths(rel_paths, pattern):
            failures.append(f"{platform_id} {role}: manifest missing required path {pattern}")

    for pattern in config.get("forbidden_paths", []):
        hits = matching_paths(rel_paths, pattern)
        if hits:
            failures.append(
                f"{platform_id} {role}: manifest contains forbidden path {hits[0]} "
                f"(pattern {pattern})"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify release artifact bundles before publishing.")
    parser.add_argument("--artifacts-root", required=True, help="Downloaded artifacts root directory")
    parser.add_argument(
        "--platform-id",
        action="append",
        default=[],
        help="Optional platform id filter (repeatable)",
    )
    args = parser.parse_args()

    artifacts_root = pathlib.Path(args.artifacts_root).resolve()
    if not artifacts_root.is_dir():
        raise SystemExit(f"Artifacts root not found: {artifacts_root}")

    targets = choose_targets(args.platform_id)
    failures: list[str] = []

    for target in targets:
        platform_id = target["platform_id"]
        try:
            metadata = load_metadata(artifacts_root, platform_id)
        except FileNotFoundError:
            failures.append(f"{platform_id}: missing metadata-{platform_id}.json")
            continue

        expected = [artifact["name"] for artifact in metadata.get("artifacts", [])]
        for name in expected:
            try:
                require_asset(artifacts_root, name)
            except FileNotFoundError:
                failures.append(f"{platform_id}: missing {name}")

        for role in ("client", "server"):
            manifest_name = target[role]["manifest_name"]
            try:
                manifest = load_manifest(artifacts_root, manifest_name)
            except FileNotFoundError:
                continue
            validate_manifest(failures, target, role, manifest)

    if failures:
        print("Artifact verification failed:")
        for line in failures:
            print(f"- {line}")
        return 1

    print(f"Verified release artifacts in {artifacts_root}")
    for target in targets:
        print(f"- {target['platform_id']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
