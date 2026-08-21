#!/usr/bin/env python3

from __future__ import annotations

import argparse
import ctypes
import fnmatch
import hashlib
import json
import os
import pathlib
import re
import shutil
import stat
import struct
import subprocess
import tarfile
import sys
import tempfile
import zipfile
from collections.abc import Iterator
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.package_release import (
    DEFAULT_PRESERVE,
    MAX_EXPANDED_UPDATE_BYTES,
    MAX_INSTALL_JOURNAL_BYTES,
    MAX_LOCAL_MANIFEST_BYTES,
    MAX_MANAGED_FILES,
    MAX_RELEASE_ASSET_BYTES,
    MAX_UPDATE_MANIFEST_BYTES,
    MAX_UPDATE_FILE_BYTES,
    MAX_UPDATE_PACKAGE_BYTES,
    estimate_install_journal_size,
    load_release_trust_policy_document,
    validated_archive_filename,
    validated_archive_path,
)
from tools.release.targets import (
    RELEASE_NOTICE_PATHS,
    TARGETS,
    expected_artifacts,
    expected_asset_names,
    expected_role_metadata,
    get_target,
)


SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
MSI_COMPOUND_FILE_SIGNATURE = bytes.fromhex("D0CF11E0A1B11AE1")
MSI_ROOT_CLSID_LE = bytes.fromhex("84100c0000000000c000000000000046")
CFB_FREE_SECTOR = 0xFFFFFFFF
CFB_END_OF_CHAIN = 0xFFFFFFFE
CFB_MAX_REGULAR_SECTOR = 0xFFFFFFFA
MAX_UPDATER_CONFIG_SIZE = 1024 * 1024
MSI_VALIDATION_TIMEOUT_SECONDS = 30
MSI_EXTRACTION_TIMEOUT_SECONDS = 5 * 60
MAX_MSIEXTRACT_OUTPUT_BYTES = 64 * 1024 * 1024
UTC_TIMESTAMP_PATTERN = re.compile(
    r"[0-9]{4}-(?:0[1-9]|1[0-2])-(?:0[1-9]|[12][0-9]|3[01])"
    r"T(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]Z"
)


class ArtifactLayoutError(ValueError):
    """Raised when downloaded release artifacts are ambiguously laid out."""


def _is_link_or_reparse(path_status: os.stat_result) -> bool:
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return stat.S_ISLNK(path_status.st_mode) or bool(
        getattr(path_status, "st_file_attributes", 0) & reparse_flag
    )


def _artifact_entry_display(
    artifacts_root: pathlib.Path,
    path: pathlib.Path,
) -> str:
    try:
        return path.relative_to(artifacts_root).as_posix()
    except ValueError:
        return str(path)


def _require_non_redirecting_directory(
    path: pathlib.Path,
    description: str,
) -> None:
    absolute_path = pathlib.Path(os.path.abspath(path))
    current = pathlib.Path(absolute_path.anchor)
    for component in absolute_path.parts[1:]:
        current /= component
        try:
            path_status = current.lstat()
        except OSError as exc:
            raise ArtifactLayoutError(
                f"cannot inspect {description} component {current} ({exc})"
            ) from exc
        if _is_link_or_reparse(path_status):
            raise ArtifactLayoutError(
                f"{description} must not pass through a link or reparse point: "
                f"{current}"
            )
        if not stat.S_ISDIR(path_status.st_mode):
            raise ArtifactLayoutError(
                f"{description} component {current} must be a directory"
            )


def _walk_artifact_tree(
    artifacts_root: pathlib.Path,
) -> Iterator[tuple[pathlib.Path, os.stat_result]]:
    """Walk an artifact tree without following redirecting filesystem entries."""

    _require_non_redirecting_directory(artifacts_root, "artifacts root")

    pending = [artifacts_root]
    while pending:
        current = pending.pop()
        try:
            current_status = current.lstat()
        except OSError as exc:
            raise ArtifactLayoutError(
                f"cannot inspect artifact directory "
                f"{_artifact_entry_display(artifacts_root, current)} ({exc})"
            ) from exc
        if _is_link_or_reparse(current_status) or not stat.S_ISDIR(
            current_status.st_mode
        ):
            raise ArtifactLayoutError(
                f"artifact directory {_artifact_entry_display(artifacts_root, current)} "
                "must be a non-link directory"
            )
        try:
            with os.scandir(current) as scanner:
                entries = sorted(
                    scanner,
                    key=lambda entry: (entry.name.casefold(), entry.name),
                )
        except OSError as exc:
            raise ArtifactLayoutError(
                f"cannot enumerate artifact directory "
                f"{_artifact_entry_display(artifacts_root, current)} ({exc})"
            ) from exc

        child_directories: list[pathlib.Path] = []
        for entry in entries:
            path = pathlib.Path(entry.path)
            try:
                # pathlib.lstat() preserves link-count and reparse metadata on
                # Windows; DirEntry.stat(follow_symlinks=False) can return a
                # reduced result with st_nlink == 0.
                path_status = path.lstat()
            except OSError as exc:
                raise ArtifactLayoutError(
                    f"cannot inspect artifact entry "
                    f"{_artifact_entry_display(artifacts_root, path)} ({exc})"
                ) from exc
            if _is_link_or_reparse(path_status):
                raise ArtifactLayoutError(
                    f"artifact entry {_artifact_entry_display(artifacts_root, path)} "
                    "must not be a link or reparse point"
                )
            if stat.S_ISDIR(path_status.st_mode):
                child_directories.append(path)
            elif not stat.S_ISREG(path_status.st_mode):
                raise ArtifactLayoutError(
                    f"artifact entry {_artifact_entry_display(artifacts_root, path)} "
                    "must be a regular file or directory"
                )
            yield path, path_status

        pending.extend(reversed(child_directories))


def _is_metadata_filename(name: str) -> bool:
    portable_name = name.casefold()
    return portable_name.startswith("metadata-") and portable_name.endswith(".json")


def discover_bundle_metadata_files(artifacts_root: pathlib.Path) -> list[pathlib.Path]:
    """Find metadata only in a flat root or in one-level artifact bundles."""

    metadata_files: list[pathlib.Path] = []
    metadata_names: dict[str, pathlib.Path] = {}
    for path, path_status in _walk_artifact_tree(artifacts_root):
        if not _is_metadata_filename(path.name):
            continue
        if not stat.S_ISREG(path_status.st_mode):
            raise ArtifactLayoutError(
                f"metadata entry {_artifact_entry_display(artifacts_root, path)} "
                "must be a regular file"
            )
        if path_status.st_nlink != 1:
            raise ArtifactLayoutError(
                f"metadata file {_artifact_entry_display(artifacts_root, path)} "
                "must not have hard links"
            )
        portable_name = path.name.casefold()
        previous = metadata_names.get(portable_name)
        if previous is not None:
            raise ArtifactLayoutError(
                "metadata filenames have a case-insensitive collision: "
                f"{_artifact_entry_display(artifacts_root, previous)} and "
                f"{_artifact_entry_display(artifacts_root, path)}"
            )
        metadata_names[portable_name] = path
        metadata_files.append(path)

    for path in metadata_files:
        if path.name != path.name.casefold():
            raise ArtifactLayoutError(
                f"metadata filename {_artifact_entry_display(artifacts_root, path)} "
                "must use canonical lowercase spelling"
            )

    metadata_files.sort()
    layout_depths: set[int] = set()
    for path in metadata_files:
        relative_path = path.relative_to(artifacts_root)
        depth = len(relative_path.parts)
        if depth not in (1, 2):
            raise ArtifactLayoutError(
                f"metadata file {relative_path.as_posix()} must be at an artifact "
                "bundle root"
            )
        layout_depths.add(depth)

    if len(layout_depths) > 1:
        raise ArtifactLayoutError(
            "metadata files must use one layout: either directly in the artifacts "
            "root or directly in one-level artifact bundles"
        )

    if layout_depths == {2}:
        metadata_by_bundle: dict[pathlib.Path, list[pathlib.Path]] = {}
        for path in metadata_files:
            metadata_by_bundle.setdefault(path.parent, []).append(path)
        for bundle_root, paths in metadata_by_bundle.items():
            if len(paths) > 1:
                relative_bundle = bundle_root.relative_to(artifacts_root).as_posix()
                raise ArtifactLayoutError(
                    f"artifact bundle {relative_bundle} contains multiple metadata files"
                )

    return metadata_files


def require_bundle_asset(
    artifacts_root: pathlib.Path,
    bundle_root: pathlib.Path,
    name: str,
) -> pathlib.Path:
    """Resolve one exact, direct asset without recursive or cross-bundle fallback."""

    if not isinstance(name, str):
        raise ArtifactLayoutError(f"release asset name is not a plain filename: {name!r}")
    try:
        validated_name = validated_archive_filename(name, "release asset name")
    except SystemExit as exc:
        raise ArtifactLayoutError(str(exc)) from exc
    if validated_name != name:
        raise ArtifactLayoutError(f"release asset name is not canonical: {name!r}")

    try:
        relative_bundle = bundle_root.relative_to(artifacts_root)
    except ValueError as exc:
        raise ArtifactLayoutError(
            f"artifact bundle root {bundle_root} is outside {artifacts_root}"
        ) from exc
    if len(relative_bundle.parts) > 1:
        raise ArtifactLayoutError(
            f"artifact bundle root {relative_bundle.as_posix()} is nested too deeply"
        )

    portable_name = name.casefold()
    all_matches: list[tuple[pathlib.Path, os.stat_result]] = []
    for path, path_status in _walk_artifact_tree(artifacts_root):
        if path.name.casefold() == portable_name:
            all_matches.append((path, path_status))

    direct_matches = [
        (path, path_status)
        for path, path_status in all_matches
        if path.parent == bundle_root and path.name == name
    ]
    if len(direct_matches) != 1:
        raise FileNotFoundError(
            f"{name} must be an exact direct file in artifact bundle {bundle_root}"
        )

    if len(all_matches) != 1:
        locations = ", ".join(
            path.relative_to(artifacts_root).as_posix()
            for path, _ in all_matches
        )
        raise ArtifactLayoutError(
            f"release asset {name!r} is not unique under {artifacts_root} "
            f"({locations or 'no exact match'})"
        )

    direct_path, direct_status = direct_matches[0]
    if not stat.S_ISREG(direct_status.st_mode):
        raise ArtifactLayoutError(
            f"release asset {name!r} in {bundle_root} must be a regular file"
        )
    if direct_status.st_nlink != 1:
        raise ArtifactLayoutError(
            f"release asset {name!r} in {bundle_root} must not have hard links"
        )
    if all_matches[0][0] != direct_path:
        raise ArtifactLayoutError(
            f"release asset {name!r} does not exactly match its bundle-root filename"
        )
    return direct_path


def strict_json_bytes(
    payload: bytes,
    description: str,
    maximum_size: int = MAX_UPDATER_CONFIG_SIZE,
) -> Any:
    def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"{description} contains duplicate field {key!r}")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ValueError(f"{description} contains non-finite number {value}")

    if not payload or len(payload) > maximum_size:
        raise ValueError(f"{description} has an unsupported size")
    return json.loads(
        payload.decode("utf-8", errors="strict"),
        object_pairs_hook=reject_duplicate_keys,
        parse_constant=reject_constant,
    )


def validate_msi_compound_file(installer_path: pathlib.Path) -> None:
    file_size = installer_path.stat().st_size
    with installer_path.open("rb") as handle:
        header = handle.read(512)
        if len(header) != 512 or header[:8] != MSI_COMPOUND_FILE_SIGNATURE:
            raise ValueError("missing Compound File header")
        if header[8:24] != bytes(16):
            raise ValueError("nonzero Compound File header CLSID")

        major_version = struct.unpack_from("<H", header, 26)[0]
        byte_order = struct.unpack_from("<H", header, 28)[0]
        sector_shift = struct.unpack_from("<H", header, 30)[0]
        mini_sector_shift = struct.unpack_from("<H", header, 32)[0]
        directory_sector_count = struct.unpack_from("<I", header, 40)[0]
        fat_sector_count = struct.unpack_from("<I", header, 44)[0]
        first_directory_sector = struct.unpack_from("<I", header, 48)[0]
        mini_stream_cutoff = struct.unpack_from("<I", header, 56)[0]
        first_difat_sector = struct.unpack_from("<I", header, 68)[0]
        difat_sector_count = struct.unpack_from("<I", header, 72)[0]

        expected_sector_shift = {3: 9, 4: 12}.get(major_version)
        if expected_sector_shift is None or sector_shift != expected_sector_shift:
            raise ValueError("invalid Compound File version/sector size")
        if byte_order != 0xFFFE or mini_sector_shift != 6 or header[34:40] != bytes(6):
            raise ValueError("invalid Compound File byte order or reserved fields")
        if major_version == 3 and directory_sector_count != 0:
            raise ValueError("invalid version 3 directory sector count")
        if mini_stream_cutoff != 0x1000:
            raise ValueError("invalid Compound File mini-stream cutoff")

        sector_size = 1 << sector_shift
        if file_size < sector_size * 3 or file_size % sector_size != 0:
            raise ValueError("truncated or misaligned Compound File")
        total_sectors = file_size // sector_size - 1
        if fat_sector_count == 0 or fat_sector_count > total_sectors:
            raise ValueError("invalid Compound File FAT sector count")
        if first_directory_sector > CFB_MAX_REGULAR_SECTOR or first_directory_sector >= total_sectors:
            raise ValueError("invalid Compound File directory sector")

        header_difat = struct.unpack_from("<109I", header, 76)
        header_fat_sectors = [sector for sector in header_difat if sector <= CFB_MAX_REGULAR_SECTOR]
        required_header_fat_sectors = min(fat_sector_count, len(header_difat))
        if len(header_fat_sectors) < required_header_fat_sectors:
            raise ValueError("incomplete Compound File header DIFAT")
        checked_fat_sectors = header_fat_sectors[:required_header_fat_sectors]
        if len(set(checked_fat_sectors)) != len(checked_fat_sectors) or any(
            sector >= total_sectors for sector in checked_fat_sectors
        ):
            raise ValueError("invalid Compound File FAT sector reference")
        if first_directory_sector in checked_fat_sectors:
            raise ValueError("directory sector aliases a FAT sector")
        if fat_sector_count > len(header_difat) and (
            first_difat_sector > CFB_MAX_REGULAR_SECTOR
            or first_difat_sector >= total_sectors
            or difat_sector_count == 0
        ):
            raise ValueError("missing Compound File DIFAT chain")

        handle.seek((first_directory_sector + 1) * sector_size)
        root_entry = handle.read(128)
        if len(root_entry) != 128:
            raise ValueError("truncated Compound File root directory entry")
        name_length = struct.unpack_from("<H", root_entry, 64)[0]
        if name_length != 22 or root_entry[66] != 5:
            raise ValueError("invalid Compound File root directory entry")
        if root_entry[:name_length].decode("utf-16le") != "Root Entry\0":
            raise ValueError("invalid Compound File root directory name")
        if root_entry[80:96] != MSI_ROOT_CLSID_LE:
            raise ValueError("Compound File root is not a Windows Installer database")


def _validate_msi_with_windows_installer(installer_path: pathlib.Path) -> None:
    """Open the database and prove that it contains an installable file row."""

    from ctypes import wintypes

    error_success = 0
    error_no_more_items = 259
    database_handle = wintypes.UINT(0)
    view_handle = wintypes.UINT(0)
    record_handle = wintypes.UINT(0)

    try:
        # Restrict DLL resolution to System32; the verifier can run in a directory
        # containing untrusted downloaded release assets.
        msi = ctypes.WinDLL(
            "msi.dll",
            use_last_error=True,
            winmode=0x00000800,  # LOAD_LIBRARY_SEARCH_SYSTEM32
        )
    except (AttributeError, OSError) as exc:
        raise ValueError("Windows Installer API is unavailable") from exc

    try:
        msi.MsiOpenDatabaseW.argtypes = (
            wintypes.LPCWSTR,
            wintypes.LPCWSTR,
            ctypes.POINTER(wintypes.UINT),
        )
        msi.MsiOpenDatabaseW.restype = wintypes.UINT
        msi.MsiDatabaseOpenViewW.argtypes = (
            wintypes.UINT,
            wintypes.LPCWSTR,
            ctypes.POINTER(wintypes.UINT),
        )
        msi.MsiDatabaseOpenViewW.restype = wintypes.UINT
        msi.MsiViewExecute.argtypes = (wintypes.UINT, wintypes.UINT)
        msi.MsiViewExecute.restype = wintypes.UINT
        msi.MsiViewFetch.argtypes = (
            wintypes.UINT,
            ctypes.POINTER(wintypes.UINT),
        )
        msi.MsiViewFetch.restype = wintypes.UINT
        msi.MsiCloseHandle.argtypes = (wintypes.UINT,)
        msi.MsiCloseHandle.restype = wintypes.UINT
    except AttributeError as exc:
        raise ValueError("Windows Installer API is incomplete") from exc

    def require_success(result: int, operation: str) -> None:
        if result != error_success:
            raise ValueError(
                f"{operation} failed with Windows Installer error {result}"
            )

    try:
        # A null persistence-mode pointer is MSIDBOPEN_READONLY.
        require_success(
            msi.MsiOpenDatabaseW(
                str(installer_path),
                None,
                ctypes.byref(database_handle),
            ),
            "opening MSI database",
        )
        require_success(
            msi.MsiDatabaseOpenViewW(
                database_handle,
                "SELECT `File`, `FileSize` FROM `File`",
                ctypes.byref(view_handle),
            ),
            "opening MSI File table",
        )
        require_success(
            msi.MsiViewExecute(view_handle, 0),
            "executing MSI File table query",
        )
        result = msi.MsiViewFetch(view_handle, ctypes.byref(record_handle))
        if result == error_no_more_items:
            raise ValueError("MSI File table contains no payload rows")
        require_success(result, "reading MSI File table")
    finally:
        if record_handle.value:
            msi.MsiCloseHandle(record_handle)
        if view_handle.value:
            msi.MsiCloseHandle(view_handle)
        if database_handle.value:
            msi.MsiCloseHandle(database_handle)


def _run_msiinfo(msiinfo: str, *arguments: str) -> str:
    try:
        result = subprocess.run(
            [msiinfo, *arguments],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=MSI_VALIDATION_TIMEOUT_SECONDS,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ValueError(f"msiinfo could not inspect the MSI ({exc})") from exc
    if result.returncode != 0:
        detail = result.stderr.strip().replace("\r", " ").replace("\n", " ")
        if len(detail) > 512:
            detail = detail[:512] + "..."
        raise ValueError(
            f"msiinfo rejected the MSI with exit code {result.returncode}"
            + (f" ({detail})" if detail else "")
        )
    return result.stdout


def _validate_msi_with_msiinfo(installer_path: pathlib.Path) -> None:
    msiinfo = shutil.which("msiinfo")
    if not msiinfo:
        raise ValueError(
            "msiinfo from msitools is required for semantic MSI validation"
        )

    tables = {
        line.strip()
        for line in _run_msiinfo(msiinfo, "tables", str(installer_path)).splitlines()
        if line.strip()
    }
    if "File" not in tables:
        raise ValueError("MSI database does not contain a File table")

    exported_file_table = _run_msiinfo(
        msiinfo,
        "export",
        str(installer_path),
        "File",
    )
    rows = [line for line in exported_file_table.splitlines() if line.strip()]
    required_columns = {"File", "FileSize"}
    if not rows or not required_columns.issubset(
        field.strip() for field in rows[0].split("\t")
    ):
        raise ValueError("msiinfo returned an invalid File table schema")
    # libmsi's IDT export has three schema/metadata rows before its data rows.
    if len(rows) <= 3:
        raise ValueError("MSI File table contains no payload rows")


def validate_msi_database(installer_path: pathlib.Path) -> None:
    if sys.platform == "win32":
        _validate_msi_with_windows_installer(installer_path)
    else:
        _validate_msi_with_msiinfo(installer_path)


def sha256_file(path: pathlib.Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def validate_installer_asset(
    failures: list[str],
    platform_id: str,
    installer_path: pathlib.Path,
    installer_type: str,
) -> None:
    if installer_type != "msi":
        failures.append(f"{platform_id}: unsupported installer type {installer_type!r}")
        return
    try:
        validate_msi_compound_file(installer_path)
    except (OSError, ValueError, struct.error, UnicodeDecodeError) as exc:
        failures.append(f"{platform_id}: {installer_path.name} is not a structurally valid MSI ({exc})")
        return
    try:
        validate_msi_database(installer_path)
    except (OSError, ValueError) as exc:
        failures.append(
            f"{platform_id}: {installer_path.name} is not a usable MSI database ({exc})"
        )


def _run_msiextract(msiextract: str, *arguments: str) -> str:
    timeout_seconds = (
        MSI_EXTRACTION_TIMEOUT_SECONDS
        if "--directory" in arguments
        else MSI_VALIDATION_TIMEOUT_SECONDS
    )
    try:
        result = subprocess.run(
            [msiextract, *arguments],
            check=False,
            capture_output=True,
            timeout=timeout_seconds,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ValueError(f"msiextract could not inspect the MSI ({exc})") from exc

    if len(result.stdout) > MAX_MSIEXTRACT_OUTPUT_BYTES:
        raise ValueError("msiextract output exceeds its verification limit")
    if result.returncode != 0:
        detail = result.stderr[:512].decode("utf-8", errors="replace")
        detail = detail.strip().replace("\r", " ").replace("\n", " ")
        if len(result.stderr) > 512:
            detail += "..."
        raise ValueError(
            f"msiextract rejected the MSI with exit code {result.returncode}"
            + (f" ({detail})" if detail else "")
        )
    try:
        return result.stdout.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise ValueError("msiextract returned non-UTF-8 paths") from exc


def _validated_msi_payload_expectations(
    manifest: dict[str, Any],
    local_manifest_name: str,
) -> dict[str, tuple[str, int | None, str | None]]:
    files = manifest.get("files")
    if not isinstance(files, list):
        raise ValueError("client manifest has an invalid files payload")
    if len(files) > MAX_MANAGED_FILES:
        raise ValueError("client manifest exceeds the managed-file limit")

    expected: dict[str, tuple[str, int | None, str | None]] = {}
    for entry in files:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise ValueError("client manifest contains an invalid file entry")
        try:
            path = validated_archive_path(entry["path"], "MSI manifest file path")
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
        size = entry.get("size")
        digest = entry.get("sha256")
        if type(size) is not int or size < 0 or size > MAX_UPDATE_FILE_BYTES:
            raise ValueError(f"client manifest has an invalid size for {path!r}")
        if not isinstance(digest, str) or not SHA256_PATTERN.fullmatch(digest):
            raise ValueError(f"client manifest has an invalid sha256 for {path!r}")
        key = path.casefold()
        if key in expected:
            raise ValueError(f"client manifest contains duplicate path {path!r}")
        expected[key] = (path, size, digest.casefold())

    try:
        local_path = validated_archive_filename(
            local_manifest_name,
            "MSI local install manifest name",
        )
    except SystemExit as exc:
        raise ValueError(str(exc)) from exc
    local_key = local_path.casefold()
    if local_key in expected:
        raise ValueError("client manifest manages its local install manifest")
    expected[local_key] = (local_path, None, None)

    ordered = sorted(expected)
    for parent, child in zip(ordered, ordered[1:]):
        if child.startswith(parent + "/"):
            raise ValueError(
                "client manifest contains a file/directory prefix collision "
                f"between {expected[parent][0]!r} and {expected[child][0]!r}"
            )
    return expected


def _validated_msi_listing(listing: str) -> list[str]:
    raw_paths = listing.split("\n")
    if raw_paths and raw_paths[-1] == "":
        raw_paths.pop()
    if not raw_paths:
        raise ValueError("msiextract listed no payload files")
    if len(raw_paths) > MAX_MANAGED_FILES + 1:
        raise ValueError("MSI payload exceeds the managed-file limit")

    paths: list[str] = []
    seen: dict[str, str] = {}
    for raw_path in raw_paths:
        if raw_path.endswith("\r"):
            raw_path = raw_path[:-1]
        try:
            path = validated_archive_path(raw_path, "MSI payload path")
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
        key = path.casefold()
        if key in seen:
            raise ValueError(
                f"MSI payload path {path!r} collides with {seen[key]!r}"
            )
        seen[key] = path
        paths.append(path)

    ordered = sorted(seen)
    for parent, child in zip(ordered, ordered[1:]):
        if child.startswith(parent + "/"):
            raise ValueError(
                "MSI payload contains a file/directory prefix collision "
                f"between {seen[parent]!r} and {seen[child]!r}"
            )
    return paths


def _resolve_msi_install_prefix(
    listed_paths: list[str],
    expected: dict[str, tuple[str, int | None, str | None]],
) -> tuple[str, ...]:
    expected_paths = [entry[0] for entry in expected.values()]
    expected_set = set(expected_paths)
    candidates: set[tuple[str, ...]] = set()

    for listed_path in listed_paths:
        listed_parts = tuple(listed_path.split("/"))
        for expected_path in expected_paths:
            expected_parts = tuple(expected_path.split("/"))
            if (
                len(listed_parts) >= len(expected_parts)
                and listed_parts[-len(expected_parts) :] == expected_parts
            ):
                candidates.add(listed_parts[: -len(expected_parts)])

    matches: list[tuple[str, ...]] = []
    for prefix in candidates:
        relative_paths: list[str] = []
        for listed_path in listed_paths:
            parts = tuple(listed_path.split("/"))
            if len(parts) <= len(prefix) or parts[: len(prefix)] != prefix:
                break
            relative_paths.append("/".join(parts[len(prefix) :]))
        else:
            if set(relative_paths) == expected_set and len(relative_paths) == len(expected_set):
                matches.append(prefix)

    if len(matches) != 1:
        raise ValueError(
            "MSI File table does not describe exactly one install root matching "
            "the client manifest"
        )
    return matches[0]


def _scan_extracted_msi_tree(
    extraction_root: pathlib.Path,
) -> tuple[dict[str, str], dict[str, tuple[str, pathlib.Path, os.stat_result]]]:
    directories: dict[str, str] = {}
    files: dict[str, tuple[str, pathlib.Path, os.stat_result]] = {}

    def visit(directory: pathlib.Path, relative_parts: tuple[str, ...]) -> None:
        try:
            entries = sorted(os.scandir(directory), key=lambda entry: entry.name)
        except OSError as exc:
            raise ValueError(f"cannot inspect extracted MSI directory ({exc})") from exc
        for entry in entries:
            child_parts = (*relative_parts, entry.name)
            raw_path = "/".join(child_parts)
            try:
                path = validated_archive_path(raw_path, "extracted MSI path")
            except SystemExit as exc:
                raise ValueError(str(exc)) from exc
            key = path.casefold()
            try:
                status = entry.stat(follow_symlinks=False)
            except OSError as exc:
                raise ValueError(f"cannot inspect extracted MSI path {path!r} ({exc})") from exc
            if stat.S_ISLNK(status.st_mode):
                raise ValueError(f"extracted MSI path {path!r} is a symbolic link")
            if stat.S_ISDIR(status.st_mode):
                if key in directories or key in files:
                    raise ValueError(f"extracted MSI path {path!r} has a case collision")
                directories[key] = path
                visit(pathlib.Path(entry.path), child_parts)
            elif stat.S_ISREG(status.st_mode):
                if key in files or key in directories:
                    raise ValueError(f"extracted MSI path {path!r} has a case collision")
                files[key] = (path, pathlib.Path(entry.path), status)
            else:
                raise ValueError(f"extracted MSI path {path!r} is not a regular file")

    visit(extraction_root, ())
    return directories, files


def _expected_parent_directories(paths: list[str]) -> dict[str, str]:
    directories: dict[str, str] = {}
    for path in paths:
        parts = path.split("/")
        for length in range(1, len(parts)):
            parent = "/".join(parts[:length])
            key = parent.casefold()
            previous = directories.get(key)
            if previous is not None and previous != parent:
                raise ValueError(
                    f"MSI payload directory {parent!r} collides with {previous!r}"
                )
            directories[key] = parent
    return directories


def _validate_extracted_local_manifest(
    payload: bytes,
    external_manifest: dict[str, Any],
) -> None:
    try:
        local_manifest = strict_json_bytes(
            payload,
            "MSI local install manifest",
            MAX_LOCAL_MANIFEST_BYTES,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, TypeError, ValueError) as exc:
        raise ValueError(f"MSI local install manifest is invalid ({exc})") from exc
    if not isinstance(local_manifest, dict):
        raise ValueError("MSI local install manifest root is not an object")
    generated_at = local_manifest.get("generated_at_utc")
    if not isinstance(generated_at, str) or not UTC_TIMESTAMP_PATTERN.fullmatch(generated_at):
        raise ValueError("MSI local install manifest has an invalid generated_at_utc")

    expected_local_manifest = dict(external_manifest)
    expected_local_manifest.pop("package", None)
    expected_local_manifest["generated_at_utc"] = generated_at
    expected_local_manifest["install_state"] = True
    if local_manifest != expected_local_manifest:
        raise ValueError("MSI local install manifest does not match the client manifest")

    expected_payload = json.dumps(expected_local_manifest, indent=2).encode("utf-8")
    if payload != expected_payload:
        raise ValueError("MSI local install manifest is not canonically serialized")


def _validate_msi_payload_against_manifest(
    installer_path: pathlib.Path,
    manifest: dict[str, Any],
    local_manifest_name: str,
) -> None:
    msiextract = shutil.which("msiextract")
    if not msiextract:
        raise ValueError(
            "msiextract from msitools is required to bind MSI payload contents"
        )

    expected = _validated_msi_payload_expectations(manifest, local_manifest_name)
    listing = _run_msiextract(msiextract, "--list", str(installer_path))
    listed_paths = _validated_msi_listing(listing)
    install_prefix = _resolve_msi_install_prefix(listed_paths, expected)

    with tempfile.TemporaryDirectory(prefix="worr-msi-verify-") as temp_dir:
        extraction_root = pathlib.Path(temp_dir) / "payload"
        extraction_root.mkdir(mode=0o700)
        _run_msiextract(
            msiextract,
            "--directory",
            str(extraction_root),
            str(installer_path),
        )

        directories, extracted_files = _scan_extracted_msi_tree(extraction_root)
        listed_by_key = {path.casefold(): path for path in listed_paths}
        if set(extracted_files) != set(listed_by_key):
            missing = sorted(set(listed_by_key) - set(extracted_files))
            extra = sorted(set(extracted_files) - set(listed_by_key))
            detail = []
            if missing:
                detail.append(f"missing {listed_by_key[missing[0]]!r}")
            if extra:
                detail.append(f"unexpected {extracted_files[extra[0]][0]!r}")
            raise ValueError("extracted MSI file set differs from its File table (" + ", ".join(detail) + ")")
        for key, listed_path in listed_by_key.items():
            if extracted_files[key][0] != listed_path:
                raise ValueError(
                    f"extracted MSI path {extracted_files[key][0]!r} does not exactly "
                    f"match File-table path {listed_path!r}"
                )

        expected_directories = _expected_parent_directories(listed_paths)
        if directories != expected_directories:
            missing = sorted(set(expected_directories) - set(directories))
            extra = sorted(set(directories) - set(expected_directories))
            detail = []
            if missing:
                detail.append(f"missing directory {expected_directories[missing[0]]!r}")
            if extra:
                detail.append(f"unexpected directory {directories[extra[0]]!r}")
            if not detail:
                detail.append("directory path case differs")
            raise ValueError("extracted MSI directory set differs from its File table (" + ", ".join(detail) + ")")

        expanded_size = 0
        local_key = local_manifest_name.casefold()
        for listed_path in listed_paths:
            parts = tuple(listed_path.split("/"))
            relative_path = "/".join(parts[len(install_prefix) :])
            expected_path, expected_size, expected_sha256 = expected[relative_path.casefold()]
            extracted_path, full_path, status = extracted_files[listed_path.casefold()]
            if relative_path != expected_path:
                raise ValueError(
                    f"MSI payload path {relative_path!r} does not exactly match "
                    f"client manifest path {expected_path!r}"
                )
            expanded_size += status.st_size
            if expanded_size > MAX_EXPANDED_UPDATE_BYTES:
                raise ValueError("MSI payload exceeds the expanded-size limit")

            if relative_path.casefold() == local_key:
                if status.st_size > MAX_LOCAL_MANIFEST_BYTES:
                    raise ValueError("MSI local install manifest exceeds its size limit")
                try:
                    payload = full_path.read_bytes()
                except OSError as exc:
                    raise ValueError(f"cannot read MSI local install manifest ({exc})") from exc
                _validate_extracted_local_manifest(payload, manifest)
                continue

            if status.st_size != expected_size:
                raise ValueError(
                    f"MSI payload file {expected_path!r} size mismatch "
                    f"({status.st_size} != {expected_size})"
                )
            try:
                actual_sha256 = sha256_file(full_path)
            except OSError as exc:
                raise ValueError(f"cannot hash MSI payload file {expected_path!r} ({exc})") from exc
            if actual_sha256 != expected_sha256:
                raise ValueError(f"MSI payload file {expected_path!r} sha256 mismatch")


def validate_msi_payload(
    failures: list[str],
    platform_id: str,
    installer_path: pathlib.Path,
    manifest: dict[str, Any],
    local_manifest_name: str,
) -> None:
    try:
        _validate_msi_payload_against_manifest(
            installer_path,
            manifest,
            local_manifest_name,
        )
    except (OSError, ValueError) as exc:
        failures.append(
            f"{platform_id}: {installer_path.name} payload does not match the "
            f"client manifest ({exc})"
        )


def choose_targets(platform_ids: list[str]) -> list[dict[str, Any]]:
    if platform_ids:
        return [get_target(platform_id) for platform_id in platform_ids]
    return list(TARGETS)


def require_asset(
    bundle_root: pathlib.Path,
    name: str,
    *,
    artifacts_root: pathlib.Path | None = None,
) -> pathlib.Path:
    return require_bundle_asset(artifacts_root or bundle_root, bundle_root, name)


def load_manifest_path(
    manifest_path: pathlib.Path,
    name: str,
) -> dict[str, Any]:
    manifest_size = manifest_path.stat().st_size
    if manifest_size > MAX_UPDATE_MANIFEST_BYTES:
        raise ValueError(
            f"manifest is {manifest_size} bytes; the updater supports at most "
            f"{MAX_UPDATE_MANIFEST_BYTES} bytes"
        )
    with manifest_path.open("rb") as handle:
        payload = handle.read(MAX_UPDATE_MANIFEST_BYTES + 1)
    if len(payload) > MAX_UPDATE_MANIFEST_BYTES:
        raise ValueError(
            f"manifest exceeds the updater's {MAX_UPDATE_MANIFEST_BYTES}-byte limit"
        )
    return strict_json_bytes(payload, f"manifest {name}", MAX_UPDATE_MANIFEST_BYTES)


def load_manifest(
    bundle_root: pathlib.Path,
    name: str,
    *,
    artifacts_root: pathlib.Path | None = None,
) -> dict[str, Any]:
    return load_manifest_path(
        require_asset(
            bundle_root,
            name,
            artifacts_root=artifacts_root,
        ),
        name,
    )


def load_metadata(artifacts_root: pathlib.Path, platform_id: str) -> dict[str, Any]:
    metadata_name = f"metadata-{platform_id}.json"
    metadata_path = require_asset(artifacts_root, metadata_name)
    with metadata_path.open("rb") as handle:
        payload = handle.read(MAX_UPDATE_MANIFEST_BYTES + 1)
    return strict_json_bytes(
        payload, f"release metadata {platform_id}", MAX_UPDATE_MANIFEST_BYTES
    )


def discover_metadata_files(
    artifacts_root: pathlib.Path,
    failures: list[str],
) -> dict[str, pathlib.Path]:
    expected_names = {
        f"metadata-{target['platform_id']}.json": target["platform_id"]
        for target in TARGETS
    }
    candidates: dict[str, list[pathlib.Path]] = {
        platform_id: [] for platform_id in expected_names.values()
    }
    try:
        metadata_files = discover_bundle_metadata_files(artifacts_root)
    except (OSError, ArtifactLayoutError) as exc:
        failures.append(f"invalid artifact metadata layout ({exc})")
        return {}

    for path in metadata_files:
        platform_id = expected_names.get(path.name)
        if platform_id is None:
            failures.append(f"unexpected metadata file {path.relative_to(artifacts_root).as_posix()}")
            continue
        candidates[platform_id].append(path)

    discovered: dict[str, pathlib.Path] = {}
    for platform_id, paths in candidates.items():
        if len(paths) > 1:
            failures.append(f"{platform_id}: duplicate metadata files")
            continue
        if paths:
            discovered[platform_id] = paths[0]
    return discovered


def matching_paths(paths: list[str], pattern: str) -> list[str]:
    return [path for path in paths if fnmatch.fnmatch(path, pattern)]


def hash_stream(handle: Any) -> str:
    hasher = hashlib.sha256()
    for chunk in iter(lambda: handle.read(1024 * 1024), b""):
        hasher.update(chunk)
    return hasher.hexdigest()


def validate_archive_members(
    failures: list[str],
    *,
    platform_id: str,
    role: str,
    manifest_name: str,
    package_path: pathlib.Path,
    archive_format: str,
    files: list[Any],
    local_manifest_name: str,
    external_manifest: dict[str, Any] | None = None,
    expected_trust_policy: dict[str, Any] | None = None,
    release_metadata: dict[str, Any] | None = None,
) -> None:
    prefix = f"{platform_id} {role}: {manifest_name}"
    try:
        package_size = package_path.stat().st_size
    except OSError as exc:
        failures.append(f"{prefix} cannot inspect package size ({exc})")
        return
    if package_size > MAX_UPDATE_PACKAGE_BYTES:
        failures.append(
            f"{prefix} package is {package_size} bytes; the updater supports at most "
            f"{MAX_UPDATE_PACKAGE_BYTES} bytes"
        )
        return
    try:
        local_manifest_key = validated_archive_path(
            local_manifest_name,
            "local manifest name",
        ).casefold()
    except SystemExit:
        local_manifest_key = ""

    expected: dict[str, tuple[str, int, str, int]] = {}
    for entry in files:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            continue
        try:
            path = validated_archive_path(entry["path"], "manifest file path")
        except SystemExit:
            continue
        size = entry.get("size")
        sha256 = entry.get("sha256")
        mode = entry.get("mode", 0o644)
        if type(size) is not int or size < 0:
            continue
        if not isinstance(sha256, str) or not SHA256_PATTERN.fullmatch(sha256):
            continue
        path_key = path.casefold()
        if path_key == local_manifest_key:
            failures.append(
                f"{prefix} external manifest files must not include local install manifest {path!r}"
            )
            continue
        if type(mode) is not int or mode not in (0o644, 0o755):
            continue
        expected.setdefault(path_key, (path, size, sha256.casefold(), mode))

    observed: set[str] = set()
    expanded_size = 0

    def validate_member(name: str, size: int, mode: int, open_member: Any) -> None:
        nonlocal expanded_size
        try:
            path = validated_archive_path(name, "archive member path")
        except SystemExit:
            failures.append(f"{prefix} contains unsafe archive member {name!r}")
            return

        path_key = path.casefold()
        if path_key in observed:
            failures.append(f"{prefix} contains duplicate archive member {path!r}")
            return
        observed.add(path_key)

        maximum_member_size = (
            MAX_LOCAL_MANIFEST_BYTES
            if path_key == local_manifest_key
            else MAX_UPDATE_FILE_BYTES
        )
        if size < 0 or size > maximum_member_size:
            failures.append(
                f"{prefix} archive member {path!r} exceeds its expanded size limit"
            )
            return
        expanded_size += size
        if expanded_size > MAX_EXPANDED_UPDATE_BYTES:
            failures.append(
                f"{prefix} archive expands beyond {MAX_EXPANDED_UPDATE_BYTES} bytes"
            )
            return

        if path_key == local_manifest_key:
            if path != local_manifest_name:
                failures.append(
                    f"{prefix} archive local manifest path {path!r} does not exactly match "
                    f"{local_manifest_name!r}"
                )
            else:
                try:
                    if size > MAX_LOCAL_MANIFEST_BYTES:
                        raise ValueError(
                            "local install manifest exceeds its maximum size"
                        )
                    with open_member() as handle:
                        local_manifest = strict_json_bytes(
                            handle.read(MAX_LOCAL_MANIFEST_BYTES + 1),
                            "local install manifest",
                            MAX_LOCAL_MANIFEST_BYTES,
                        )
                except (
                    OSError,
                    RuntimeError,
                    EOFError,
                    UnicodeDecodeError,
                    json.JSONDecodeError,
                    TypeError,
                    ValueError,
                ) as exc:
                    failures.append(f"{prefix} cannot read local install manifest ({exc})")
                    return
                if not isinstance(local_manifest, dict):
                    failures.append(f"{prefix} local install manifest root is not an object")
                    return
                if external_manifest is not None:
                    expected_local_manifest = dict(external_manifest)
                    expected_local_manifest.pop("package", None)
                    expected_local_manifest.pop("generated_at_utc", None)
                    expected_local_manifest["install_state"] = True
                    actual_local_manifest = dict(local_manifest)
                    actual_local_manifest.pop("generated_at_utc", None)
                    if actual_local_manifest != expected_local_manifest:
                        failures.append(f"{prefix} local install manifest does not match external manifest")
                elif local_manifest.get("install_state") is not True:
                    failures.append(f"{prefix} local install manifest is not marked as install state")
            return

        expected_entry = expected.get(path_key)
        if expected_entry is None:
            failures.append(f"{prefix} contains unexpected archive member {path!r}")
            return

        expected_path, expected_size, expected_sha256, expected_mode = expected_entry
        if path != expected_path:
            failures.append(
                f"{prefix} archive member path {path!r} does not exactly match {expected_path!r}"
            )
            return
        if size != expected_size:
            failures.append(
                f"{prefix} archive member {expected_path!r} size mismatch "
                f"({size} != {expected_size})"
            )
            return
        if mode != expected_mode:
            failures.append(
                f"{prefix} archive member {expected_path!r} mode mismatch "
                f"({mode:o} != {expected_mode:o})"
            )
            return
        try:
            with open_member() as handle:
                actual_sha256 = hash_stream(handle)
        except (OSError, RuntimeError, EOFError, zipfile.BadZipFile, tarfile.TarError) as exc:
            failures.append(f"{prefix} cannot read archive member {expected_path!r} ({exc})")
            return
        if actual_sha256 != expected_sha256:
            failures.append(f"{prefix} archive member {expected_path!r} sha256 mismatch")
            return
        if expected_path == "worr_update.json" and expected_trust_policy is not None:
            try:
                with open_member() as handle:
                    updater_config = strict_json_bytes(
                        handle.read(MAX_UPDATER_CONFIG_SIZE + 1),
                        "embedded updater config",
                    )
            except (
                OSError,
                RuntimeError,
                EOFError,
                UnicodeDecodeError,
                json.JSONDecodeError,
                TypeError,
                ValueError,
            ) as exc:
                failures.append(f"{prefix} cannot validate embedded updater config ({exc})")
                return
            if not isinstance(updater_config, dict):
                failures.append(f"{prefix} embedded updater config root is not an object")
            elif updater_config.get("release_trust_policy") != expected_trust_policy:
                failures.append(
                    f"{prefix} embedded updater trust policy differs from the signer policy"
                )
            elif "allow_unsigned_development_index" in updater_config:
                failures.append(
                    f"{prefix} production updater config contains the unsigned development override"
                )
            elif release_metadata is not None and external_manifest is not None:
                expected_config = {
                    "repo": release_metadata.get("repo"),
                    "channel": release_metadata.get("channel"),
                    "role": role,
                    "release_index_asset": (
                        f"worr-release-index-{release_metadata.get('channel')}.json"
                    ),
                    "launch_exe": external_manifest.get("launch_exe"),
                    "autolaunch": True,
                    "allow_prerelease": release_metadata.get("channel") == "nightly",
                    "preserve": list(DEFAULT_PRESERVE),
                    "release_trust_policy": expected_trust_policy,
                }
                actual_keys = set(updater_config)
                expected_keys = set(expected_config)
                if actual_keys != expected_keys:
                    missing_keys = sorted(expected_keys - actual_keys)
                    unexpected_keys = sorted(actual_keys - expected_keys)
                    failures.append(
                        f"{prefix} embedded updater config keys mismatch "
                        f"(missing={missing_keys!r}, unexpected={unexpected_keys!r})"
                    )
                for key, expected_value in expected_config.items():
                    if updater_config.get(key) != expected_value:
                        failures.append(
                            f"{prefix} embedded updater config {key} mismatch "
                            f"({updater_config.get(key)!r} != {expected_value!r})"
                        )

    try:
        if archive_format == "zip":
            with zipfile.ZipFile(package_path) as archive:
                for info in archive.infolist():
                    if info.is_dir():
                        failures.append(f"{prefix} contains unexpected archive directory {info.filename!r}")
                        continue
                    if not (info.flag_bits & 0x800) and not info.filename.isascii():
                        failures.append(
                            f"{prefix} contains non-ASCII archive member without the UTF-8 flag "
                            f"{info.filename!r}"
                        )
                        continue
                    unix_mode = info.external_attr >> 16
                    if unix_mode & 0o7000:
                        failures.append(
                            f"{prefix} contains privileged permission bits on {info.filename!r}"
                        )
                        continue
                    if (unix_mode & 0o170000) == 0o120000:
                        failures.append(f"{prefix} contains symbolic-link member {info.filename!r}")
                        continue
                    if info.create_system == 3 and (unix_mode & 0o170000) not in (0, 0o100000):
                        failures.append(f"{prefix} contains non-regular archive member {info.filename!r}")
                        continue
                    if info.flag_bits & 0x1:
                        failures.append(f"{prefix} contains encrypted archive member {info.filename!r}")
                        continue
                    validate_member(
                        info.filename,
                        info.file_size,
                        (
                            0o755
                            if info.create_system == 3
                            and (unix_mode & 0o111)
                            else 0o644
                        ),
                        lambda info=info: archive.open(info, "r"),
                    )
        elif archive_format == "tar.gz":
            with tarfile.open(package_path, "r:gz") as archive:
                for info in archive.getmembers():
                    if not info.isreg():
                        failures.append(f"{prefix} contains non-regular archive member {info.name!r}")
                        continue
                    if info.mode & 0o7000:
                        failures.append(
                            f"{prefix} contains privileged permission bits on {info.name!r}"
                        )
                        continue
                    validate_member(
                        info.name,
                        info.size,
                        0o755 if info.mode & 0o111 else 0o644,
                        lambda info=info: archive.extractfile(info),
                    )
        else:
            failures.append(f"{prefix} has unsupported package format {archive_format!r}")
            return
    except (OSError, EOFError, zipfile.BadZipFile, tarfile.TarError) as exc:
        failures.append(f"{prefix} package is not a valid {archive_format} archive ({exc})")
        return

    for path_key, (path, _, _, _) in expected.items():
        if path_key not in observed:
            failures.append(f"{prefix} archive missing manifest file {path!r}")
    if local_manifest_key not in observed:
        failures.append(f"{prefix} archive missing local install manifest {local_manifest_name!r}")


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
    package_path: pathlib.Path | None = None,
    package_format: str | None = None,
    release_metadata: dict[str, Any] | None = None,
    expected_trust_policy: dict[str, Any] | None = None,
) -> None:
    config = target[role]
    platform_id = target["platform_id"]

    package = manifest.get("package", {})
    if not isinstance(package, dict):
        failures.append(f"{platform_id} {role}: {manifest_name} has invalid package metadata")
        package = {}
    if package.get("name") != package_name:
        failures.append(
            f"{platform_id} {role}: {manifest_name} package name mismatch "
            f"({package.get('name')} != {package_name})"
        )
    package_size = package.get("size")
    package_sha256 = package.get("sha256")
    if package_format is not None and package.get("format") != package_format:
        failures.append(
            f"{platform_id} {role}: {manifest_name} package format mismatch "
            f"({package.get('format')!r} != {package_format!r})"
        )
    if type(package_size) is not int or package_size <= 0:
        failures.append(f"{platform_id} {role}: {manifest_name} has invalid package size")
    elif package_size > MAX_UPDATE_PACKAGE_BYTES:
        failures.append(
            f"{platform_id} {role}: {manifest_name} declares a {package_size}-byte package; "
            f"the updater supports at most {MAX_UPDATE_PACKAGE_BYTES} bytes"
        )
    if not isinstance(package_sha256, str) or not SHA256_PATTERN.fullmatch(package_sha256):
        failures.append(f"{platform_id} {role}: {manifest_name} has invalid package sha256")
    package_asset_within_limit = True
    if package_path is not None:
        actual_size = package_path.stat().st_size
        if actual_size > MAX_UPDATE_PACKAGE_BYTES:
            failures.append(
                f"{platform_id} {role}: {manifest_name} package asset is {actual_size} bytes; "
                f"the updater supports at most {MAX_UPDATE_PACKAGE_BYTES} bytes"
            )
            package_asset_within_limit = False
        if package_size != actual_size:
            failures.append(
                f"{platform_id} {role}: {manifest_name} package size mismatch "
                f"({package_size!r} != {actual_size})"
            )
        if (
            package_asset_within_limit
            and isinstance(package_sha256, str)
            and SHA256_PATTERN.fullmatch(package_sha256)
        ):
            actual_sha256 = sha256_file(package_path)
            if package_sha256.casefold() != actual_sha256:
                failures.append(f"{platform_id} {role}: {manifest_name} package sha256 mismatch")

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

    if release_metadata is not None:
        if manifest.get("schema_version") != 3:
            failures.append(f"{platform_id} {role}: {manifest_name} schema_version mismatch")
        for key in ("repo", "channel", "version"):
            if manifest.get(key) != release_metadata.get(key):
                failures.append(
                    f"{platform_id} {role}: {manifest_name} {key} mismatch "
                    f"({manifest.get(key)!r} != {release_metadata.get(key)!r})"
                )
        expected_platform = {
            "id": target["platform_id"],
            "os": target["os"],
            "arch": target["arch"],
        }
        if manifest.get("platform") != expected_platform:
            failures.append(f"{platform_id} {role}: {manifest_name} platform contract mismatch")
        expected_build = {
            "id": release_metadata.get("build_id"),
            "commit_sha": release_metadata.get("commit_sha"),
        }
        if manifest.get("build") != expected_build:
            failures.append(f"{platform_id} {role}: {manifest_name} build identity mismatch")

    files = manifest.get("files", [])
    if not isinstance(files, list):
        failures.append(f"{platform_id} {role}: {manifest_name} has invalid files payload")
        return
    if len(files) > MAX_MANAGED_FILES:
        failures.append(
            f"{platform_id} {role}: {manifest_name} has {len(files)} managed files; "
            f"the updater supports at most {MAX_MANAGED_FILES}"
        )
        return

    rel_paths: list[str] = []
    journal_paths: list[str] = []
    file_sizes: dict[str, Any] = {}
    seen_paths: dict[str, str] = {}
    manifest_expanded_size = 0
    for entry in files:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            failures.append(f"{platform_id} {role}: {manifest_name} has an invalid file entry")
            continue

        source_path = entry["path"]
        try:
            rel_path = validated_archive_path(source_path, "manifest file path")
        except SystemExit:
            failures.append(
                f"{platform_id} {role}: {manifest_name} has unsafe file path {source_path!r}"
            )
            continue
        journal_paths.append(rel_path)

        path_key = rel_path.casefold()
        previous = seen_paths.get(path_key)
        if previous is not None:
            failures.append(
                f"{platform_id} {role}: {manifest_name} has duplicate file path "
                f"{rel_path!r} (conflicts with {previous!r})"
            )
            continue
        seen_paths[path_key] = rel_path
        rel_paths.append(rel_path)
        file_size = entry.get("size")
        file_sha256 = entry.get("sha256")
        file_mode = entry.get("mode")
        if type(file_size) is not int or file_size < 0:
            failures.append(
                f"{platform_id} {role}: {manifest_name} has invalid size for {rel_path}"
            )
        elif file_size > MAX_UPDATE_FILE_BYTES:
            failures.append(
                f"{platform_id} {role}: {manifest_name} file {rel_path!r} "
                f"exceeds {MAX_UPDATE_FILE_BYTES} bytes"
            )
        else:
            manifest_expanded_size += file_size
        if not isinstance(file_sha256, str) or not SHA256_PATTERN.fullmatch(file_sha256):
            failures.append(
                f"{platform_id} {role}: {manifest_name} has invalid sha256 for {rel_path}"
            )
        if type(file_mode) is not int or file_mode not in (0o644, 0o755):
            failures.append(
                f"{platform_id} {role}: {manifest_name} has invalid mode for {rel_path}"
            )
        file_sizes[rel_path] = file_size

    if manifest_expanded_size > MAX_EXPANDED_UPDATE_BYTES:
        failures.append(
            f"{platform_id} {role}: {manifest_name} expands beyond "
            f"{MAX_EXPANDED_UPDATE_BYTES} bytes"
        )

    for child_key in sorted(seen_paths):
        components = child_key.split("/")
        for component_count in range(len(components) - 1, 0, -1):
            parent_key = "/".join(components[:component_count])
            if parent_key not in seen_paths:
                continue
            failures.append(
                f"{platform_id} {role}: {manifest_name} has file/directory prefix "
                f"collision {seen_paths[parent_key]!r} and {seen_paths[child_key]!r}"
            )
            break

    rel_paths.sort()

    journal_size = estimate_install_journal_size(
        journal_paths,
        config["local_manifest_name"],
    )
    if journal_size > MAX_INSTALL_JOURNAL_BYTES:
        failures.append(
            f"{platform_id} {role}: {manifest_name} needs a worst-case "
            f"{journal_size}-byte install journal; the updater supports at most "
            f"{MAX_INSTALL_JOURNAL_BYTES} bytes"
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

    for notice_path in RELEASE_NOTICE_PATHS:
        if notice_path not in required_paths:
            continue
        size = file_sizes.get(notice_path)
        if not isinstance(size, int) or size <= 0:
            failures.append(f"{platform_id} {role}: manifest has empty release notice {notice_path}")

    if (
        package_path is not None
        and package_format is not None
        and package_asset_within_limit
    ):
        validate_archive_members(
            failures,
            platform_id=platform_id,
            role=role,
            manifest_name=manifest_name,
            package_path=package_path,
            archive_format=package_format,
            files=files,
            local_manifest_name=config["local_manifest_name"],
            external_manifest=manifest,
            expected_trust_policy=expected_trust_policy,
            release_metadata=release_metadata,
        )


def validate_metadata_contract(
    target: dict[str, Any],
    metadata: dict[str, Any],
    failures: list[str],
) -> None:
    platform_id = target["platform_id"]
    if metadata.get("schema_version") != 3:
        failures.append(
            f"{platform_id}: metadata schema_version mismatch "
            f"({metadata.get('schema_version')!r} != 3)"
        )

    for key in ("platform_id", "platform_stub", "os", "arch", "archive_format"):
        if metadata.get(key) != target[key]:
            failures.append(
                f"{platform_id}: metadata {key} mismatch ({metadata.get(key)!r} != {target[key]!r})"
            )
    if metadata.get("autoupdater") != target["autoupdater"]:
        failures.append(f"{platform_id}: metadata autoupdater contract mismatch")
    for key in ("repo", "channel", "version", "commit_sha", "build_id"):
        if not isinstance(metadata.get(key), str) or not metadata[key]:
            failures.append(f"{platform_id}: metadata has invalid {key}")

    metadata_artifacts = metadata.get("artifacts")
    if not isinstance(metadata_artifacts, list):
        failures.append(f"{platform_id}: metadata has invalid artifacts payload")
        metadata_artifacts = []

    actual_artifacts: dict[str, dict[str, Any]] = {}
    for artifact in metadata_artifacts:
        if not isinstance(artifact, dict) or not isinstance(artifact.get("name"), str):
            failures.append(f"{platform_id}: metadata has invalid artifact entry")
            continue
        name = artifact["name"]
        if name in actual_artifacts:
            failures.append(f"{platform_id}: metadata has duplicate artifact {name}")
            continue
        actual_artifacts[name] = artifact

    expected_by_name = {artifact["name"]: artifact for artifact in expected_artifacts(target)}
    for name, expected in expected_by_name.items():
        actual = actual_artifacts.get(name)
        if actual is None:
            failures.append(f"{platform_id}: metadata missing required artifact {name}")
            continue
        for key in ("kind", "role", "variant"):
            if actual.get(key) != expected[key]:
                failures.append(
                    f"{platform_id}: metadata artifact {name} {key} mismatch "
                    f"({actual.get(key)!r} != {expected[key]!r})"
                )
    for name in actual_artifacts.keys() - expected_by_name.keys():
        failures.append(f"{platform_id}: metadata has unexpected artifact {name}")

    actual_roles = metadata.get("roles")
    if not isinstance(actual_roles, dict):
        failures.append(f"{platform_id}: metadata has invalid roles payload")
        actual_roles = {}
    expected_roles = expected_role_metadata(target)
    for role, expected in expected_roles.items():
        actual = actual_roles.get(role)
        if not isinstance(actual, dict):
            failures.append(f"{platform_id}: metadata missing required role {role}")
            continue
        for key, expected_value in expected.items():
            if actual.get(key) != expected_value:
                failures.append(
                    f"{platform_id}: metadata {role} {key} mismatch "
                    f"({actual.get(key)!r} != {expected_value!r})"
                )
    for role in actual_roles.keys() - expected_roles.keys():
        failures.append(f"{platform_id}: metadata has unexpected role {role}")


def validate_target_bundle(
    bundle_root: pathlib.Path,
    target: dict[str, Any],
    metadata: dict[str, Any],
    failures: list[str],
    expected_trust_policy: dict[str, Any] | None = None,
    *,
    artifacts_root: pathlib.Path | None = None,
) -> None:
    platform_id = target["platform_id"]
    validate_metadata_contract(target, metadata, failures)
    search_root = artifacts_root or bundle_root

    registry_names = expected_asset_names(target)
    resolved_assets: dict[str, pathlib.Path] = {}
    for name in registry_names:
        try:
            asset_path = require_asset(
                bundle_root,
                name,
                artifacts_root=search_root,
            )
        except FileNotFoundError:
            failures.append(f"{platform_id}: missing {name}")
            continue
        except (OSError, ArtifactLayoutError) as exc:
            failures.append(f"{platform_id}: invalid asset binding for {name} ({exc})")
            continue
        resolved_assets[name] = asset_path
        try:
            asset_size = asset_path.stat().st_size
        except OSError as exc:
            failures.append(f"{platform_id}: cannot inspect {name} size ({exc})")
            continue
        if asset_size > MAX_RELEASE_ASSET_BYTES:
            failures.append(
                f"{platform_id}: {name} is {asset_size} bytes; GitHub release "
                f"assets must be at most {MAX_RELEASE_ASSET_BYTES} bytes"
            )
            continue
        installer = target.get("installer")
        if installer and name == installer["name"]:
            validate_installer_asset(failures, platform_id, asset_path, installer["type"])

    for role in ("client", "server"):
        for manifest_key, package_key, required_key, forbidden_key in (
            ("manifest_name", "package_name", "required_paths", "forbidden_paths"),
            ("update_manifest_name", "update_package_name", "update_required_paths", "update_forbidden_paths"),
        ):
            manifest_name = target[role][manifest_key]
            if manifest_name not in resolved_assets:
                continue
            try:
                manifest = load_manifest_path(
                    resolved_assets[manifest_name],
                    manifest_name,
                )
            except FileNotFoundError:
                # The registry-level check above reports the missing artifact.
                continue
            except (OSError, UnicodeDecodeError, ValueError) as exc:
                failures.append(f"{platform_id} {role}: invalid {manifest_name} ({exc})")
                continue
            if not isinstance(manifest, dict):
                failures.append(f"{platform_id} {role}: {manifest_name} root is not an object")
                continue
            package_path = resolved_assets.get(target[role][package_key])
            validate_manifest(
                failures,
                target,
                role,
                manifest,
                manifest_name=manifest_name,
                package_name=target[role][package_key],
                required_paths=target[role].get(required_key, target[role].get("required_paths", [])),
                forbidden_paths=target[role].get(forbidden_key, target[role].get("forbidden_paths", [])),
                package_path=package_path,
                package_format=target["archive_format"] if manifest_key == "manifest_name" else "zip",
                release_metadata=metadata,
                expected_trust_policy=expected_trust_policy,
            )
            installer = target.get("installer")
            if role == "client" and manifest_key == "manifest_name" and installer:
                installer_path = resolved_assets.get(installer["name"])
                if installer_path is not None:
                    validate_msi_payload(
                        failures,
                        platform_id,
                        installer_path,
                        manifest,
                        target[role]["local_manifest_name"],
                    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify release artifact bundles before publishing.")
    parser.add_argument("--artifacts-root", required=True, help="Downloaded artifacts root directory")
    parser.add_argument(
        "--release-trust-policy",
        help="Canonical policy that every packaged updater config must embed",
    )
    parser.add_argument(
        "--platform-id",
        action="append",
        default=[],
        help="Optional platform id filter (repeatable)",
    )
    args = parser.parse_args()

    artifacts_root = pathlib.Path(os.path.abspath(args.artifacts_root))
    if not artifacts_root.is_dir():
        raise SystemExit(f"Artifacts root not found: {artifacts_root}")

    targets = choose_targets(args.platform_id)
    failures: list[str] = []
    metadata_files = discover_metadata_files(artifacts_root, failures)
    release_identity: dict[str, Any] | None = None

    for target in targets:
        platform_id = target["platform_id"]
        metadata_path = metadata_files.get(platform_id)
        if metadata_path is None:
            failures.append(f"{platform_id}: missing metadata-{platform_id}.json")
            continue
        try:
            with metadata_path.open("rb") as handle:
                metadata_payload = handle.read(MAX_UPDATE_MANIFEST_BYTES + 1)
            metadata = strict_json_bytes(
                metadata_payload,
                f"release metadata {platform_id}",
                MAX_UPDATE_MANIFEST_BYTES,
            )
        except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
            failures.append(f"{platform_id}: invalid metadata file ({exc})")
            continue
        if not isinstance(metadata, dict):
            failures.append(f"{platform_id}: metadata root is not an object")
            continue

        current_identity = {
            key: metadata.get(key)
            for key in ("repo", "channel", "version", "commit_sha")
        }
        if release_identity is None:
            release_identity = current_identity
        else:
            for key, expected_value in release_identity.items():
                if current_identity[key] != expected_value:
                    failures.append(
                        f"{platform_id}: metadata {key} differs across platform bundles "
                        f"({current_identity[key]!r} != {expected_value!r})"
                    )

        expected_trust_policy = None
        if args.release_trust_policy:
            try:
                expected_trust_policy = load_release_trust_policy_document(
                    pathlib.Path(args.release_trust_policy),
                    expected_repo=metadata.get("repo", ""),
                    expected_channel=metadata.get("channel", ""),
                )
            except SystemExit as exc:
                failures.append(
                    f"{platform_id}: invalid signer release trust policy ({exc})"
                )
        validate_target_bundle(
            metadata_path.parent,
            target,
            metadata,
            failures,
            expected_trust_policy=expected_trust_policy,
            artifacts_root=artifacts_root,
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
