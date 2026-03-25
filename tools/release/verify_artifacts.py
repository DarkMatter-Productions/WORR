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
    *,
    manifest_name: str,
    package_name: str,
    required_paths: list[str],
    forbidden_paths: list[str],
) -> None:
    config = target[role]
    platform_id = target["platform_id"]

    package = manifest.get("package", {})
    if package.get("name") != package_name:
        failures.append(
            f"{platform_id} {role}: {manifest_name} package name mismatch "
            f"({package.get('name')} != {package_name})"
        )

    if manifest.get("role") != role:
        failures.append(f"{platform_id} {role}: {manifest_name} role mismatch ({manifest.get('role')})")
    launch_exe = manifest.get("launch_exe", manifest.get("launcher_exe"))
    if launch_exe != config["launch_exe"]:
        failures.append(
            f"{platform_id} {role}: {manifest_name} launch executable mismatch "
            f"({launch_exe} != {config['launch_exe']})"
        )
    engine_library = manifest.get("engine_library", manifest.get("runtime_exe"))
    if engine_library != config["engine_library"]:
        failures.append(
            f"{platform_id} {role}: {manifest_name} engine library mismatch "
            f"({engine_library} != {config['engine_library']})"
        )
    if manifest.get("local_manifest_name") != config["local_manifest_name"]:
        failures.append(
            f"{platform_id} {role}: {manifest_name} local manifest mismatch "
            f"({manifest.get('local_manifest_name')} != {config['local_manifest_name']})"
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

    for pattern in required_paths:
        if not matching_paths(rel_paths, pattern):
            failures.append(f"{platform_id} {role}: manifest missing required path {pattern}")

    for pattern in forbidden_paths:
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
            for manifest_key, package_key, required_key, forbidden_key in (
                ("manifest_name", "package_name", "required_paths", "forbidden_paths"),
                ("update_manifest_name", "update_package_name", "update_required_paths", "update_forbidden_paths"),
            ):
                manifest_name = target[role][manifest_key]
                try:
                    manifest = load_manifest(artifacts_root, manifest_name)
                except FileNotFoundError:
                    continue
                validate_manifest(
                    failures,
                    target,
                    role,
                    manifest,
                    manifest_name=manifest_name,
                    package_name=target[role][package_key],
                    required_paths=target[role].get(required_key, target[role].get("required_paths", [])),
                    forbidden_paths=target[role].get(forbidden_key, target[role].get("forbidden_paths", [])),
                )

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
