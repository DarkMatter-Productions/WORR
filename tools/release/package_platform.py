#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import subprocess
import sys
import tempfile
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.release.stage_release_layout import stage_release_layout
from tools.release.targets import get_target


def run_command(command: list[str]) -> None:
    subprocess.run(command, check=True)


def metadata_artifacts(target: dict[str, Any]) -> list[dict[str, str]]:
    artifacts: list[dict[str, str]] = []
    for role in ("client", "server"):
        config = target[role]
        artifacts.append(
            {
                "name": config["package_name"],
                "kind": "archive",
                "role": role,
                "variant": "manual",
            }
        )
        artifacts.append(
            {
                "name": config["manifest_name"],
                "kind": "manifest",
                "role": role,
                "variant": "manual",
            }
        )
        artifacts.append(
            {
                "name": config["update_package_name"],
                "kind": "archive",
                "role": role,
                "variant": "update",
            }
        )
        artifacts.append(
            {
                "name": config["update_manifest_name"],
                "kind": "manifest",
                "role": role,
                "variant": "update",
            }
        )
    installer = target.get("installer")
    if installer:
        artifacts.append(
            {
                "name": installer["name"],
                "kind": installer["type"],
                "role": "installer",
                "variant": "manual",
            }
        )
    return artifacts


def metadata_roles(target: dict[str, Any]) -> dict[str, dict[str, str]]:
    roles: dict[str, dict[str, str]] = {}
    for role in ("client", "server"):
        config = target[role]
        roles[role] = {
            "role": config["role"],
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "package_name": config["package_name"],
            "manifest_name": config["manifest_name"],
            "update_package_name": config["update_package_name"],
            "update_manifest_name": config["update_manifest_name"],
            "local_manifest_name": config["local_manifest_name"],
        }
    return roles


def main() -> int:
    parser = argparse.ArgumentParser(description="Package release assets for a target platform.")
    parser.add_argument("--input-dir", required=True, help="Staged install directory")
    parser.add_argument("--output-dir", required=True, help="Release output directory")
    parser.add_argument("--platform-id", required=True, help="Release platform id")
    parser.add_argument("--repo", required=True, help="GitHub owner/repo")
    parser.add_argument("--channel", required=True, help="Release channel")
    parser.add_argument("--version", required=True, help="Version string")
    parser.add_argument("--commit-sha", default="", help="Source commit sha")
    parser.add_argument("--build-id", default="", help="Build id for traceability")
    parser.add_argument("--allow-prerelease", action="store_true", help="Enable prerelease updates in config")
    parser.add_argument("--write-config", dest="write_config", action="store_true", default=True,
                        help="Write updater config into staged input")
    parser.add_argument("--no-write-config", dest="write_config", action="store_false",
                        help="Do not write updater config into staged input")
    parser.add_argument("--metadata-path", help="Output metadata file path")
    parser.add_argument("--installer-path", help="Optional installer path to include if present")
    args = parser.parse_args()

    target = get_target(args.platform_id)
    input_dir = pathlib.Path(args.input_dir).resolve()
    output_dir = pathlib.Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    package_script = pathlib.Path(__file__).resolve().parents[1] / "package_release.py"
    if not package_script.is_file():
        raise SystemExit(f"Packaging script not found: {package_script}")

    release_index_asset = f"worr-release-index-{args.channel}.json"

    with tempfile.TemporaryDirectory(prefix=f"worr-release-{target['platform_id']}-") as temp_dir:
        release_input = pathlib.Path(temp_dir) / "release-input"
        release_layout = target.get("release_layout", {})
        stage_release_layout(
            input_dir,
            release_input,
            relocate_root_files=release_layout.get("relocate_root_files", []),
            relocate_root_dir=release_layout.get("relocate_root_dir", "bin"),
        )

        for role in ("client", "server"):
            config = target[role]
            base_command = [
                sys.executable,
                str(package_script),
                "--input-dir",
                str(release_input),
                "--output-dir",
                str(output_dir),
                "--version",
                args.version,
                "--repo",
                args.repo,
                "--role",
                role,
                "--channel",
                args.channel,
                "--launch-exe",
                config["launch_exe"],
                "--engine-library",
                config["engine_library"],
                "--release-index-asset",
                release_index_asset,
                "--local-manifest-name",
                config["local_manifest_name"],
                "--platform-id",
                target["platform_id"],
                "--platform-os",
                target["os"],
                "--platform-arch",
                target["arch"],
                "--build-id",
                args.build_id,
                "--commit-sha",
                args.commit_sha,
            ]
            if args.allow_prerelease:
                base_command.append("--allow-prerelease")
            if args.write_config:
                base_command.append("--write-config")

            manual_command = base_command + [
                "--package-name",
                config["package_name"],
                "--manifest-name",
                config["manifest_name"],
                "--archive-format",
                target["archive_format"],
            ]
            for pattern in config.get("include", []):
                manual_command.extend(["--include", pattern])
            for pattern in config.get("exclude", []):
                manual_command.extend(["--exclude", pattern])
            run_command(manual_command)

            update_command = base_command + [
                "--package-name",
                config["update_package_name"],
                "--manifest-name",
                config["update_manifest_name"],
                "--archive-format",
                "zip",
            ]
            for pattern in config.get("update_include", config.get("include", [])):
                update_command.extend(["--include", pattern])
            for pattern in config.get("update_exclude", config.get("exclude", [])):
                update_command.extend(["--exclude", pattern])
            run_command(update_command)

    metadata_path = pathlib.Path(args.metadata_path).resolve() if args.metadata_path else (
        output_dir / f"metadata-{target['platform_id']}.json"
    )
    installer_present = False
    installer_name = None
    if args.installer_path:
        installer = pathlib.Path(args.installer_path).resolve()
        if installer.is_file():
            installer_present = True
            installer_name = installer.name

    artifacts = metadata_artifacts(target)
    if target.get("installer") and not installer_present:
        artifacts = [entry for entry in artifacts if entry["role"] != "installer"]
    if installer_present and installer_name:
        for entry in artifacts:
            if entry["role"] == "installer":
                entry["name"] = installer_name

    metadata = {
        "schema_version": 3,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "platform_id": target["platform_id"],
        "platform_stub": target["platform_stub"],
        "os": target["os"],
        "arch": target["arch"],
        "channel": args.channel,
        "version": args.version,
        "commit_sha": args.commit_sha,
        "build_id": args.build_id,
        "archive_format": target["archive_format"],
        "autoupdater": target["autoupdater"],
        "roles": metadata_roles(target),
        "artifacts": artifacts,
    }
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(f"Wrote {metadata_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
