#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import datetime as dt
import fnmatch
import hashlib
import json
import os
import pathlib
import stat
import sys
import tarfile
import tempfile
import zipfile

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from tools.release.release_signing import (
    ReleaseSigningError,
    SIGNATURE_ALGORITHM,
    TRUST_POLICY_SCHEMA_VERSION,
    load_trust_policy,
)


DEFAULT_PRESERVE = [
    "worr_update.json",
    "worr_update_state.json",
    "basew/*.cfg",
    "basew/autoexec.cfg",
    "basew/config.cfg",
    "basew/save/*",
    "basew/saves/*",
    "basew/screenshots/*",
    "basew/demos/*",
    "basew/logs/*",
]

WINDOWS_RESERVED_NAMES = {"con", "prn", "aux", "nul", "conin$", "conout$", "clock$"}
WINDOWS_INVALID_PATH_CHARS = frozenset('<>:"|?*')
MAX_ARCHIVE_PATH_BYTES = 4096
MAX_ARCHIVE_COMPONENT_BYTES = 255
MAX_MANAGED_FILES = 99_999
MAX_UPDATE_MANIFEST_BYTES = 32 * 1024 * 1024
# GitHub requires each release asset to be strictly smaller than 2 GiB.
MAX_RELEASE_ASSET_BYTES = 2 * 1024 * 1024 * 1024 - 1
MAX_UPDATE_PACKAGE_BYTES = MAX_RELEASE_ASSET_BYTES
MAX_INSTALL_JOURNAL_BYTES = 8 * 1024 * 1024
MAX_UPDATE_FILE_BYTES = 4 * 1024 * 1024 * 1024
MAX_EXPANDED_UPDATE_BYTES = 16 * 1024 * 1024 * 1024
MAX_LOCAL_MANIFEST_BYTES = 160 * 1024 * 1024
LOCAL_INSTALL_MANIFEST_NAME = "worr_install_manifest.json"
INSTALL_JOURNAL_BASE_BYTES = 128
INSTALL_JOURNAL_PATH_OVERHEAD_BYTES = 64


def _validate_portable_path_component(component: str, label: str) -> None:
    if not component or component in (".", ".."):
        raise SystemExit(f"Invalid {label}: unsafe path component {component!r}")
    if component.endswith((" ", ".")):
        raise SystemExit(f"Invalid {label}: non-portable path component {component!r}")
    try:
        component_size = len(component.encode("utf-8"))
    except UnicodeEncodeError as exc:
        raise SystemExit(f"Invalid {label}: path component is not valid UTF-8 {component!r}") from exc
    if component_size > MAX_ARCHIVE_COMPONENT_BYTES:
        raise SystemExit(f"Invalid {label}: path component is too long {component!r}")
    if not component.isascii():
        raise SystemExit(f"Invalid {label}: non-ASCII path component {component!r}")
    if any(ord(char) < 32 or char in WINDOWS_INVALID_PATH_CHARS for char in component):
        raise SystemExit(f"Invalid {label}: non-portable path component {component!r}")

    stem = component.split(".", 1)[0].casefold()
    if stem in WINDOWS_RESERVED_NAMES or (
        len(stem) == 4
        and stem[:3] in ("com", "lpt")
        and stem[3:] in "123456789¹²³"
    ):
        raise SystemExit(f"Invalid {label}: reserved path component {component!r}")


def validated_archive_path(value: str, label: str) -> str:
    normalized = value.replace("\\", "/")
    if not normalized or normalized.startswith("/"):
        raise SystemExit(f"Invalid {label}: {value!r}")
    try:
        path_size = len(normalized.encode("utf-8"))
    except UnicodeEncodeError as exc:
        raise SystemExit(f"Invalid {label}: path is not valid UTF-8 {value!r}") from exc
    if path_size > MAX_ARCHIVE_PATH_BYTES:
        raise SystemExit(f"Invalid {label}: path is too long {value!r}")

    parts = normalized.split("/")
    for component in parts:
        _validate_portable_path_component(component, label)
    collision_key = "/".join(parts).casefold()
    if collision_key == ".worr-update" or collision_key.startswith(
        ".worr-update/"
    ):
        raise SystemExit(f"Invalid {label}: reserved updater state path {value!r}")
    return "/".join(parts)


def validated_archive_filename(value: str, label: str) -> str:
    normalized = validated_archive_path(value, label)
    if "/" in normalized:
        raise SystemExit(f"Invalid {label}: expected a file name, got {value!r}")
    return normalized


def validate_managed_file_count(count: int) -> None:
    if count > MAX_MANAGED_FILES:
        raise SystemExit(
            f"Package has {count} managed files; the updater supports at most "
            f"{MAX_MANAGED_FILES}"
        )


def estimate_install_journal_size(
    managed_paths: list[str],
    local_manifest_name: str,
) -> int:
    size = INSTALL_JOURNAL_BASE_BYTES
    for path in managed_paths:
        size += INSTALL_JOURNAL_PATH_OVERHEAD_BYTES + 2 * len(path.encode("utf-8"))
    size += INSTALL_JOURNAL_PATH_OVERHEAD_BYTES + 2 * len(
        local_manifest_name.encode("utf-8")
    )
    return size


def validate_install_journal_size(size: int) -> None:
    if size > MAX_INSTALL_JOURNAL_BYTES:
        raise SystemExit(
            f"Package needs a worst-case {size}-byte install journal; the updater "
            f"supports at most {MAX_INSTALL_JOURNAL_BYTES} bytes"
        )


def validate_updater_install_plan(
    managed_paths: list[str],
    local_manifest_name: str,
) -> None:
    validate_managed_file_count(len(managed_paths))
    ordered_paths = sorted(path.casefold() for path in managed_paths)
    for previous, current in zip(ordered_paths, ordered_paths[1:]):
        if current.startswith(previous + "/"):
            raise SystemExit(
                "Package contains a file/directory prefix collision: "
                f"{previous!r} and {current!r}"
            )
    validate_install_journal_size(
        estimate_install_journal_size(managed_paths, local_manifest_name)
    )


def validate_update_manifest_size(size: int) -> None:
    if size > MAX_UPDATE_MANIFEST_BYTES:
        raise SystemExit(
            f"Update manifest is {size} bytes; the updater supports at most "
            f"{MAX_UPDATE_MANIFEST_BYTES} bytes"
        )


def validate_update_package_size(size: int) -> None:
    if size > MAX_UPDATE_PACKAGE_BYTES:
        raise SystemExit(
            f"Update package is {size} bytes; the updater supports at most "
            f"{MAX_UPDATE_PACKAGE_BYTES} bytes"
        )


def require_packaged_source(input_dir: pathlib.Path, path: pathlib.Path, label: str) -> None:
    try:
        rel_path = path.relative_to(input_dir)
    except ValueError as exc:
        raise SystemExit(f"Invalid {label}: source is outside {input_dir}: {path}") from exc

    cursor = input_dir
    for component in rel_path.parts:
        cursor /= component
        if cursor.is_symlink():
            raise SystemExit(f"Invalid {label}: symbolic links are not packaged: {cursor}")

    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise SystemExit(f"Invalid {label}: unable to resolve source {path}: {exc}") from exc
    try:
        resolved.relative_to(input_dir)
    except ValueError as exc:
        raise SystemExit(f"Invalid {label}: source escapes {input_dir}: {path}") from exc
    if not resolved.is_file():
        raise SystemExit(f"Invalid {label}: source is not a regular file: {path}")


def write_json_atomic(path: pathlib.Path, payload: dict[str, object], label: str) -> None:
    if path.is_symlink():
        raise SystemExit(f"Refusing to overwrite symbolic-link {label}: {path}")

    temp_path: pathlib.Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temp_path = pathlib.Path(handle.name)
            json.dump(payload, handle, indent=2)
        os.replace(temp_path, path)
    finally:
        if temp_path is not None and temp_path.exists():
            temp_path.unlink()


def validate_non_overlapping_directories(
    input_dir: pathlib.Path,
    output_dir: pathlib.Path,
) -> None:
    if (
        output_dir == input_dir
        or input_dir in output_dir.parents
        or output_dir in input_dir.parents
    ):
        raise SystemExit(
            "Input and output directories must be disjoint: "
            f"{input_dir} and {output_dir}"
        )


def publish_staged_files_atomically(
    outputs: list[tuple[pathlib.Path, pathlib.Path]],
) -> None:
    """Publish a related set of staged files, restoring every old file on failure."""
    if not outputs:
        return

    destination_keys: set[str] = set()
    for source, destination in outputs:
        if not source.is_file() or source.is_symlink():
            raise SystemExit(f"Staged release artifact is not a regular file: {source}")
        destination_key = str(destination.resolve()).casefold()
        if destination_key in destination_keys:
            raise SystemExit(f"Duplicate release artifact destination: {destination}")
        destination_keys.add(destination_key)
        if destination.is_symlink():
            raise SystemExit(
                f"Refusing to overwrite symbolic-link release artifact: {destination}"
            )
        if destination.exists() and not destination.is_file():
            raise SystemExit(
                f"Release artifact destination is not a regular file: {destination}"
            )
        destination.parent.mkdir(parents=True, exist_ok=True)

    backups: dict[pathlib.Path, pathlib.Path] = {}
    published: list[pathlib.Path] = []
    committed = False
    try:
        for _, destination in outputs:
            if not destination.exists():
                continue
            descriptor, backup_name = tempfile.mkstemp(
                prefix=f".{destination.name}.backup.",
                dir=destination.parent,
            )
            os.close(descriptor)
            backup_path = pathlib.Path(backup_name)
            backup_path.unlink()
            os.replace(destination, backup_path)
            backups[destination] = backup_path

        for source, destination in outputs:
            os.replace(source, destination)
            published.append(destination)
        committed = True
    except BaseException as publish_error:
        rollback_failures: list[str] = []
        for destination in reversed(published):
            if destination in backups:
                continue
            try:
                destination.unlink(missing_ok=True)
            except OSError as exc:
                rollback_failures.append(f"remove {destination}: {exc}")

        for destination, backup_path in reversed(list(backups.items())):
            try:
                os.replace(backup_path, destination)
                del backups[destination]
            except OSError as exc:
                rollback_failures.append(
                    f"restore {destination} from {backup_path}: {exc}"
                )

        if rollback_failures:
            detail = "; ".join(rollback_failures)
            raise RuntimeError(
                "Release artifact publication failed and rollback was incomplete; "
                f"retained backups require recovery: {detail}"
            ) from publish_error
        raise
    finally:
        if committed:
            for backup_path in backups.values():
                backup_path.unlink(missing_ok=True)


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


def _encode_public_key(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


def _validate_release_scope(value: str, description: str) -> None:
    if (
        not value
        or value != value.strip()
        or len(value) > 255
        or any(ord(character) < 0x20 or ord(character) > 0x7E for character in value)
    ):
        raise SystemExit(
            f"Invalid release trust policy: {description} must be 1..255 trimmed printable ASCII characters"
        )


def load_release_trust_policy_document(
    path: pathlib.Path,
    *,
    expected_repo: str,
    expected_channel: str,
) -> dict[str, object]:
    _validate_release_scope(expected_repo, "expected repo")
    _validate_release_scope(expected_channel, "expected channel")
    try:
        policy = load_trust_policy(path.resolve())
    except (OSError, ReleaseSigningError) as exc:
        raise SystemExit(f"Invalid release trust policy: {exc}") from exc

    for key in policy.keys.values():
        for repo in key.repos:
            _validate_release_scope(repo, f"key {key.key_id} repo scope")
        for channel in key.channels:
            _validate_release_scope(channel, f"key {key.key_id} channel scope")

    authorized_keys = [
        key
        for key in policy.keys.values()
        if expected_repo in key.repos and expected_channel in key.channels
    ]
    if len(authorized_keys) < policy.threshold:
        raise SystemExit(
            "Invalid release trust policy: signature threshold cannot be met by keys "
            f"authorized for {expected_repo!r} channel {expected_channel!r}"
        )

    return {
        "schema_version": TRUST_POLICY_SCHEMA_VERSION,
        "threshold": policy.threshold,
        "keys": [
            {
                "key_id": key.key_id,
                "algorithm": SIGNATURE_ALGORITHM,
                "public_key": _encode_public_key(key.public_key),
                "repos": sorted(key.repos),
                "channels": sorted(key.channels),
            }
            for key in sorted(policy.keys.values(), key=lambda item: item.key_id)
        ],
    }


def resolve_release_trust_policy(
    path: str | None,
    *,
    allow_unsigned_development_config: bool,
    repo: str,
    channel: str,
) -> dict[str, object] | None:
    if path:
        if allow_unsigned_development_config:
            raise SystemExit(
                "--release-trust-policy cannot be combined with "
                "--allow-unsigned-development-config"
            )
        return load_release_trust_policy_document(
            pathlib.Path(path),
            expected_repo=repo,
            expected_channel=channel,
        )

    if not allow_unsigned_development_config:
        raise SystemExit(
            "Production package config requires --release-trust-policy; use "
            "--allow-unsigned-development-config only for local development fixtures"
        )
    if channel != "development":
        raise SystemExit(
            "--allow-unsigned-development-config is restricted to --channel development"
        )
    return None


def build_update_config(args: argparse.Namespace) -> dict[str, object]:
    config: dict[str, object] = {
        "repo": args.repo,
        "channel": args.channel,
        "role": args.role,
        "release_index_asset": args.release_index_asset,
        "launch_exe": args.launch_exe,
        "autolaunch": args.autolaunch,
        "allow_prerelease": args.allow_prerelease,
        "preserve": args.preserve or DEFAULT_PRESERVE,
    }
    trust_policy = getattr(args, "release_trust_policy_document", None)
    if trust_policy is not None:
        config["release_trust_policy"] = trust_policy
    elif getattr(args, "allow_unsigned_development_config", False):
        config["allow_unsigned_development_index"] = True
    return config


def write_update_config(args: argparse.Namespace, root: pathlib.Path) -> None:
    config_path = root / "worr_update.json"
    write_json_atomic(config_path, build_update_config(args), "updater config")


def parse_mapped_files(entries: list[str]) -> list[tuple[str, str]]:
    mapped_files: list[tuple[str, str]] = []
    for entry in entries:
        source, separator, dest = entry.partition("=")
        source = source.strip()
        dest = dest.strip()
        if separator != "=" or not source or not dest:
            raise SystemExit(f"Invalid --mapped-file entry: {entry!r} (expected source=dest)")
        mapped_files.append(
            (
                validated_archive_path(source, "--mapped-file source"),
                validated_archive_path(dest, "--mapped-file destination"),
            )
        )
    return mapped_files


def build_files_payload(entries: list[tuple[pathlib.Path, str]]) -> list[dict[str, object]]:
    files: list[dict[str, object]] = []
    expanded_size = 0
    for full_path, rel_path in entries:
        file_status = full_path.stat()
        file_size = file_status.st_size
        if file_size > MAX_UPDATE_FILE_BYTES:
            raise SystemExit(
                f"Package member {rel_path!r} is {file_size} bytes; the updater "
                f"supports at most {MAX_UPDATE_FILE_BYTES} bytes per file"
            )
        expanded_size += file_size
        if expanded_size > MAX_EXPANDED_UPDATE_BYTES:
            raise SystemExit(
                f"Package expands to more than {MAX_EXPANDED_UPDATE_BYTES} bytes"
            )
        files.append(
            {
                "path": rel_path,
                "sha256": hash_file(full_path),
                "size": file_size,
                "mode": (
                    0o755
                    if stat.S_IMODE(file_status.st_mode) & 0o111
                    else 0o644
                ),
            }
        )
    return files


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
    package_size = package_path.stat().st_size
    validate_update_package_size(package_size)
    manifest = build_manifest_base(args, files)
    manifest["package"] = {
        "name": args.package_name,
        "format": args.archive_format,
        "sha256": hash_file(package_path),
        "size": package_size,
    }
    return manifest


def serialize_update_manifest(manifest: dict[str, object]) -> bytes:
    payload = json.dumps(manifest, indent=2).encode("utf-8")
    validate_update_manifest_size(len(payload))
    return payload


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
        with tarfile.open(package_path, "w:gz", dereference=True) as archive:
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
    parser.add_argument(
        "--release-trust-policy",
        help="Schema-1 public Ed25519 trust policy embedded in production updater config",
    )
    parser.add_argument(
        "--allow-unsigned-development-config",
        action="store_true",
        help="Allow an unsigned updater config only for the explicit development channel",
    )
    parser.add_argument("--local-manifest-name", default=LOCAL_INSTALL_MANIFEST_NAME,
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
    if not input_dir.is_dir():
        raise SystemExit(f"Input directory not found: {input_dir}")
    validate_non_overlapping_directories(input_dir, output_dir)

    args.package_name = validated_archive_filename(args.package_name, "--package-name")
    args.manifest_name = validated_archive_filename(args.manifest_name, "--manifest-name")
    args.local_manifest_name = validated_archive_filename(args.local_manifest_name, "--local-manifest-name")
    if args.local_manifest_name != LOCAL_INSTALL_MANIFEST_NAME:
        raise SystemExit(
            "--local-manifest-name must match the native updater contract "
            f"{LOCAL_INSTALL_MANIFEST_NAME!r}"
        )
    args.launch_exe = validated_archive_path(args.launch_exe, "--launch-exe")
    args.engine_library = validated_archive_path(args.engine_library, "--engine-library")
    args.release_index_asset = validated_archive_filename(args.release_index_asset, "--release-index-asset")
    args.release_trust_policy_document = resolve_release_trust_policy(
        args.release_trust_policy,
        allow_unsigned_development_config=args.allow_unsigned_development_config,
        repo=args.repo,
        channel=args.channel,
    )
    if args.release_trust_policy_document is not None and not args.write_config:
        raise SystemExit(
            "Production package generation requires --write-config so the validated "
            "release trust policy is embedded"
        )
    generated_names = (args.package_name, args.manifest_name, args.local_manifest_name)
    generated_destination_keys = {name.casefold() for name in generated_names}
    if len(generated_destination_keys) != len(generated_names):
        raise SystemExit("Package, external manifest, and local manifest names must be distinct")

    output_dir.mkdir(parents=True, exist_ok=True)
    mapped_files = parse_mapped_files(args.mapped_file)
    mapped_source_keys: set[str] = set()

    for source_rel, _ in mapped_files:
        if source_rel.casefold() in generated_destination_keys:
            raise SystemExit(f"Mapped source collides with generated artifact name: {source_rel}")
        mapped_source = input_dir / pathlib.PurePosixPath(source_rel)
        require_packaged_source(input_dir, mapped_source, "--mapped-file source")
        mapped_source_keys.add(source_rel.casefold())

    if args.write_config:
        write_update_config(args, input_dir)

    archive_entries: list[tuple[pathlib.Path, str]] = []
    for root, dirnames, filenames in os.walk(input_dir):
        root_path = pathlib.Path(root)
        for dirname in dirnames:
            directory = root_path / dirname
            if directory.is_symlink():
                raise SystemExit(f"Symbolic-link directories are not packaged: {directory}")
        for filename in filenames:
            full_path = root_path / filename
            require_packaged_source(input_dir, full_path, "package input")
            rel_path = validated_archive_path(
                full_path.relative_to(input_dir).as_posix(),
                "package input path",
            )
            if rel_path.casefold() in mapped_source_keys:
                continue
            if rel_path.casefold() in generated_destination_keys:
                continue
            if not should_include(rel_path, args.include, args.exclude):
                continue
            archive_entries.append((full_path, rel_path))

    seen_destinations: dict[str, str] = {}
    for _, rel_path in archive_entries:
        key = rel_path.casefold()
        previous = seen_destinations.get(key)
        if previous is not None:
            raise SystemExit(f"Duplicate archive path requested: {rel_path} conflicts with {previous}")
        seen_destinations[key] = rel_path

    for source_rel, dest_rel in mapped_files:
        full_path = input_dir / source_rel
        if dest_rel.casefold() in generated_destination_keys:
            raise SystemExit(f"Mapped destination collides with generated artifact name: {dest_rel}")
        destination_key = dest_rel.casefold()
        if destination_key in seen_destinations:
            raise SystemExit(f"Duplicate archive path requested: {dest_rel}")
        if not should_include(dest_rel, args.include, args.exclude):
            continue
        archive_entries.append((full_path, dest_rel))
        seen_destinations[destination_key] = dest_rel

    archive_entries.sort(key=lambda entry: entry[1])
    validate_updater_install_plan(
        [rel_path for _, rel_path in archive_entries],
        args.local_manifest_name,
    )
    files = build_files_payload(archive_entries)

    local_manifest_path = input_dir / args.local_manifest_name
    local_manifest = build_local_install_manifest(args, files)
    write_json_atomic(local_manifest_path, local_manifest, "local install manifest")
    local_manifest_size = local_manifest_path.stat().st_size
    if local_manifest_size > MAX_LOCAL_MANIFEST_BYTES:
        raise SystemExit(
            f"Local install manifest is {local_manifest_size} bytes; the updater "
            f"supports at most {MAX_LOCAL_MANIFEST_BYTES} bytes"
        )
    managed_expanded_size = sum(int(entry["size"]) for entry in files)
    if managed_expanded_size + local_manifest_size > MAX_EXPANDED_UPDATE_BYTES:
        raise SystemExit(
            f"Package expands to more than {MAX_EXPANDED_UPDATE_BYTES} bytes"
        )
    if any(fnmatch.fnmatch(args.local_manifest_name, pattern) for pattern in args.exclude):
        raise SystemExit(f"The required local install manifest cannot be excluded: {args.local_manifest_name}")
    local_manifest_key = args.local_manifest_name.casefold()
    if local_manifest_key in seen_destinations:
        raise SystemExit(f"Duplicate archive path requested: {args.local_manifest_name}")
    archive_entries.append((local_manifest_path, args.local_manifest_name))
    seen_destinations[local_manifest_key] = args.local_manifest_name
    archive_entries.sort(key=lambda entry: entry[1])

    package_path = output_dir / args.package_name
    manifest_path = output_dir / args.manifest_name
    temporary_package: pathlib.Path | None = None
    temporary_manifest: pathlib.Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output_dir,
            prefix=f".{args.package_name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temporary_package = pathlib.Path(handle.name)
        create_archive(temporary_package, args.archive_format, archive_entries)

        manifest = build_external_manifest(args, files, temporary_package)
        manifest_payload = serialize_update_manifest(manifest)
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=output_dir,
            prefix=f".{args.manifest_name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temporary_manifest = pathlib.Path(handle.name)
            handle.write(manifest_payload)

        publish_staged_files_atomically(
            [
                (temporary_package, package_path),
                (temporary_manifest, manifest_path),
            ]
        )
        temporary_package = None
        temporary_manifest = None
    finally:
        if temporary_package is not None and temporary_package.exists():
            temporary_package.unlink()
        if temporary_manifest is not None and temporary_manifest.exists():
            temporary_manifest.unlink()

    print(f"Wrote {package_path}")
    print(f"Wrote {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
