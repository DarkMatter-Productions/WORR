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

from tools.package_release import (
    resolve_release_trust_policy,
    validate_non_overlapping_directories,
    write_json_atomic,
)
from tools.release.stage_release_layout import stage_release_layout
from tools.release.targets import expected_artifacts, expected_role_metadata, get_target


def run_command(command: list[str]) -> None:
    subprocess.run(command, check=True)


def metadata_artifacts(target: dict[str, Any]) -> list[dict[str, str]]:
    return expected_artifacts(target)


def metadata_roles(target: dict[str, Any]) -> dict[str, dict[str, str]]:
    return expected_role_metadata(target)


def resolve_installer_path(target: dict[str, Any], value: str | None) -> pathlib.Path | None:
    installer_contract = target.get("installer")
    if not installer_contract:
        if value:
            raise SystemExit(f"{target['platform_id']} does not define an installer artifact")
        return None
    if not value:
        raise SystemExit(
            f"{target['platform_id']} requires --installer-path for {installer_contract['name']}"
        )

    installer_path = pathlib.Path(value).resolve()
    if not installer_path.is_file():
        raise SystemExit(f"Installer not found: {installer_path}")
    if installer_path.name != installer_contract["name"]:
        raise SystemExit(
            f"Installer name mismatch ({installer_path.name} != {installer_contract['name']})"
        )
    return installer_path


def validate_package_output_location(input_dir: pathlib.Path, output_dir: pathlib.Path) -> None:
    validate_non_overlapping_directories(input_dir, output_dir)


def release_trust_command_args(
    trust_policy_path: str | None,
    *,
    allow_unsigned_development_config: bool,
) -> list[str]:
    if trust_policy_path:
        return ["--release-trust-policy", trust_policy_path]
    if allow_unsigned_development_config:
        return ["--allow-unsigned-development-config"]
    return []


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
    parser.add_argument(
        "--release-trust-policy",
        help="Schema-1 public Ed25519 trust policy embedded in shipped updater configs",
    )
    parser.add_argument(
        "--allow-unsigned-development-config",
        action="store_true",
        help="Allow unsigned updater configs only for the explicit development channel",
    )
    parser.add_argument("--write-config", dest="write_config", action="store_true", default=True,
                        help="Write updater config into staged input")
    parser.add_argument("--no-write-config", dest="write_config", action="store_false",
                        help="Do not write updater config into staged input")
    parser.add_argument("--metadata-path", help="Output metadata file path")
    parser.add_argument(
        "--installer-path",
        help="Installer artifact path (required when the selected target defines an installer)",
    )
    args = parser.parse_args()

    release_trust_policy_document = resolve_release_trust_policy(
        args.release_trust_policy,
        allow_unsigned_development_config=args.allow_unsigned_development_config,
        repo=args.repo,
        channel=args.channel,
    )
    if release_trust_policy_document is not None and not args.write_config:
        raise SystemExit(
            "--no-write-config is restricted to unsigned development packaging"
        )

    target = get_target(args.platform_id)
    resolve_installer_path(target, args.installer_path)
    input_dir = pathlib.Path(args.input_dir).resolve()
    output_dir = pathlib.Path(args.output_dir).resolve()
    validate_package_output_location(input_dir, output_dir)
    metadata_path = pathlib.Path(args.metadata_path).resolve() if args.metadata_path else (
        output_dir / f"metadata-{target['platform_id']}.json"
    )
    if metadata_path == input_dir or input_dir in metadata_path.parents:
        raise SystemExit(f"Metadata output must not be inside package input: {metadata_path}")
    output_dir.mkdir(parents=True, exist_ok=True)

    package_script = pathlib.Path(__file__).resolve().parents[1] / "package_release.py"
    if not package_script.is_file():
        raise SystemExit(f"Packaging script not found: {package_script}")

    release_index_asset = f"worr-release-index-{args.channel}.json"

    with tempfile.TemporaryDirectory(prefix=f"worr-release-{target['platform_id']}-") as temp_dir:
        canonical_trust_policy_path: pathlib.Path | None = None
        if release_trust_policy_document is not None:
            canonical_trust_policy_path = pathlib.Path(temp_dir) / "release-trust-policy.json"
            canonical_trust_policy_path.write_text(
                json.dumps(release_trust_policy_document, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )
        trust_command_args = release_trust_command_args(
            str(canonical_trust_policy_path) if canonical_trust_policy_path else None,
            allow_unsigned_development_config=args.allow_unsigned_development_config,
        )
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
                *trust_command_args,
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

    artifacts = metadata_artifacts(target)

    metadata = {
        "schema_version": 3,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "platform_id": target["platform_id"],
        "platform_stub": target["platform_stub"],
        "os": target["os"],
        "arch": target["arch"],
        "repo": args.repo,
        "channel": args.channel,
        "version": args.version,
        "commit_sha": args.commit_sha,
        "build_id": args.build_id,
        "archive_format": target["archive_format"],
        "autoupdater": target["autoupdater"],
        "roles": metadata_roles(target),
        "artifacts": artifacts,
    }
    write_json_atomic(metadata_path, metadata, "platform metadata")
    print(f"Wrote {metadata_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
