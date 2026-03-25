#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import fnmatch
import hashlib
import json
import os
import pathlib
import sys
import tarfile
import zipfile


DEFAULT_PRESERVE = [
    "worr_update.json",
    "worr_update_state.json",
    "basew/*.cfg",
    "basew/autoexec.cfg",
    "basew/config.cfg",
    "basew/saves/*",
    "basew/screenshots/*",
    "basew/demos/*",
    "basew/logs/*",
]


def hash_file(path: pathlib.Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def should_include(rel_path: str, include: list[str], exclude: list[str]) -> bool:
    if include and not any(fnmatch.fnmatch(rel_path, pat) for pat in include):
        return False
    if exclude and any(fnmatch.fnmatch(rel_path, pat) for pat in exclude):
        return False
    return True


def generated_at_utc() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def build_update_config(args: argparse.Namespace) -> dict[str, object]:
    return {
        "repo": args.repo,
        "channel": args.channel,
        "role": args.role,
        "release_index_asset": args.release_index_asset,
        "launch_exe": args.launch_exe,
        "autolaunch": args.autolaunch,
        "allow_prerelease": args.allow_prerelease,
        "preserve": args.preserve or DEFAULT_PRESERVE,
    }


def write_update_config(args: argparse.Namespace, root: pathlib.Path) -> None:
    config_path = root / "worr_update.json"
    config_path.write_text(json.dumps(build_update_config(args), indent=2), encoding="utf-8")


def parse_mapped_files(entries: list[str]) -> list[tuple[str, str]]:
    mapped_files: list[tuple[str, str]] = []
    for entry in entries:
        source, separator, dest = entry.partition("=")
        source = source.strip().replace("\\", "/")
        dest = dest.strip().replace("\\", "/")
        if separator != "=" or not source or not dest:
            raise SystemExit(f"Invalid --mapped-file entry: {entry!r} (expected source=dest)")
        mapped_files.append((source, dest))
    return mapped_files


def build_files_payload(entries: list[tuple[pathlib.Path, str]]) -> list[dict[str, object]]:
    return [
        {
            "path": rel_path,
            "sha256": hash_file(full_path),
            "size": full_path.stat().st_size,
        }
        for full_path, rel_path in entries
    ]


def build_manifest_base(args: argparse.Namespace, files: list[dict[str, object]]) -> dict[str, object]:
    manifest: dict[str, object] = {
        "schema_version": 3,
        "version": args.version,
        "repo": args.repo,
        "channel": args.channel,
        "role": args.role,
        "generated_at_utc": generated_at_utc(),
        "launch_exe": args.launch_exe,
        "engine_library": args.engine_library,
        "local_manifest_name": args.local_manifest_name,
        "files": files,
    }

    if args.platform_id or args.platform_os or args.platform_arch:
        manifest["platform"] = {
            "id": args.platform_id,
            "os": args.platform_os,
            "arch": args.platform_arch,
        }

    if args.build_id or args.commit_sha:
        manifest["build"] = {
            "id": args.build_id,
            "commit_sha": args.commit_sha,
        }

    return manifest


def build_external_manifest(
    args: argparse.Namespace,
    files: list[dict[str, object]],
    package_path: pathlib.Path,
) -> dict[str, object]:
    manifest = build_manifest_base(args, files)
    manifest["package"] = {
        "name": args.package_name,
        "format": args.archive_format,
        "sha256": hash_file(package_path),
        "size": package_path.stat().st_size,
    }
    return manifest


def build_local_install_manifest(args: argparse.Namespace, files: list[dict[str, object]]) -> dict[str, object]:
    manifest = build_manifest_base(args, files)
    manifest["install_state"] = True
    return manifest


def create_archive(
    package_path: pathlib.Path,
    archive_format: str,
    archive_entries: list[tuple[pathlib.Path, str]],
) -> None:
    if archive_format == "zip":
        with zipfile.ZipFile(package_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for full_path, rel_path in archive_entries:
                archive.write(full_path, rel_path)
        return

    if archive_format == "tar.gz":
        with tarfile.open(package_path, "w:gz") as archive:
            for full_path, rel_path in archive_entries:
                archive.add(full_path, arcname=rel_path, recursive=False)
        return

    raise SystemExit(f"Unsupported archive format: {archive_format}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Package WORR release assets with manifest.")
    parser.add_argument("--input-dir", required=True, help="Staging install directory root")
    parser.add_argument("--output-dir", required=True, help="Output directory for artifacts")
    parser.add_argument("--package-name", required=True, help="Package asset name")
    parser.add_argument("--manifest-name", required=True, help="Manifest asset name")
    parser.add_argument("--version", required=True, help="Release version (semver)")
    parser.add_argument("--repo", required=True, help="GitHub owner/repo")
    parser.add_argument("--role", required=True, choices=["client", "server"], help="Payload role")
    parser.add_argument("--channel", default="stable", help="Release channel name")
    parser.add_argument("--archive-format", choices=["zip", "tar.gz"], default="zip",
                        help="Archive format for package asset")
    parser.add_argument("--platform-id", default="", help="Platform identifier")
    parser.add_argument("--platform-os", default="", help="Platform OS")
    parser.add_argument("--platform-arch", default="", help="Platform architecture")
    parser.add_argument("--build-id", default="", help="Build id")
    parser.add_argument("--commit-sha", default="", help="Git commit sha")
    parser.add_argument("--launch-exe", required=True, help="User-facing launcher binary")
    parser.add_argument("--engine-library", required=True, help="Hosted engine library relative to install root")
    parser.add_argument("--release-index-asset", required=True, help="Release index asset name for this channel")
    parser.add_argument("--local-manifest-name", default="worr_install_manifest.json",
                        help="Local installed-state manifest written into the package")
    parser.add_argument("--autolaunch", dest="autolaunch", action="store_true", default=True,
                        help="Default autolaunch checkbox")
    parser.add_argument("--no-autolaunch", dest="autolaunch", action="store_false",
                        help="Disable autolaunch checkbox")
    parser.add_argument("--allow-prerelease", action="store_true", help="Allow prerelease updates")
    parser.add_argument("--include", action="append", default=[], help="Glob pattern to include")
    parser.add_argument("--exclude", action="append", default=[], help="Glob pattern to exclude")
    parser.add_argument("--preserve", action="append", default=[], help="Preset preserve patterns for updater config")
    parser.add_argument("--write-config", action="store_true", help="Write worr_update.json into input dir")
    parser.add_argument(
        "--mapped-file",
        action="append",
        default=[],
        help="Map a staged file into a different archive path (format: source=dest, both relative to input dir)",
    )
    args = parser.parse_args()

    input_dir = pathlib.Path(args.input_dir).resolve()
    output_dir = pathlib.Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    mapped_files = parse_mapped_files(args.mapped_file)
    mapped_sources = {source for source, _ in mapped_files}

    if args.write_config:
        write_update_config(args, input_dir)

    archive_entries: list[tuple[pathlib.Path, str]] = []
    for root, _, filenames in os.walk(input_dir):
        root_path = pathlib.Path(root)
        for filename in filenames:
            full_path = root_path / filename
            rel_path = full_path.relative_to(input_dir).as_posix()
            if rel_path in mapped_sources:
                continue
            if rel_path in (args.package_name, args.manifest_name, args.local_manifest_name):
                continue
            if not should_include(rel_path, args.include, args.exclude):
                continue
            archive_entries.append((full_path, rel_path))

    seen_destinations = {rel_path for _, rel_path in archive_entries}
    for source_rel, dest_rel in mapped_files:
        full_path = input_dir / source_rel
        if not full_path.is_file():
            raise SystemExit(f"Mapped source file not found: {full_path}")
        if dest_rel in (args.package_name, args.manifest_name, args.local_manifest_name):
            raise SystemExit(f"Mapped destination collides with generated artifact name: {dest_rel}")
        if dest_rel in seen_destinations:
            raise SystemExit(f"Duplicate archive path requested: {dest_rel}")
        if not should_include(dest_rel, args.include, args.exclude):
            continue
        archive_entries.append((full_path, dest_rel))
        seen_destinations.add(dest_rel)

    archive_entries.sort(key=lambda entry: entry[1])
    files = build_files_payload(archive_entries)

    local_manifest_path = input_dir / args.local_manifest_name
    local_manifest = build_local_install_manifest(args, files)
    local_manifest_path.write_text(json.dumps(local_manifest, indent=2), encoding="utf-8")
    if should_include(args.local_manifest_name, args.include, args.exclude):
        archive_entries.append((local_manifest_path, args.local_manifest_name))
        archive_entries.sort(key=lambda entry: entry[1])

    package_path = output_dir / args.package_name
    create_archive(package_path, args.archive_format, archive_entries)

    manifest = build_external_manifest(args, files, package_path)
    manifest_path = output_dir / args.manifest_name
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print(f"Wrote {package_path}")
    print(f"Wrote {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
