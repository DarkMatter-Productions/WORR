#!/usr/bin/env python3

from __future__ import annotations

import argparse
import configparser
import datetime
import hashlib
import io
import json
import os
import pathlib
import re
import stat
import sys
import zipfile
from dataclasses import dataclass, field
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_BASELINE = pathlib.Path(__file__).with_name("dependency_baseline.json")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
IMMUTABLE_GIT_REVISION_PATTERN = re.compile(r"[0-9a-f]{40,64}")
VERSIONED_DIRECTORY_PATTERN = re.compile(
    r"^(?P<family>.+)-(?P<version>[0-9][0-9A-Za-z._+~-]*)$"
)
PROJECT_PATTERN = re.compile(r"\bproject\s*\((?P<body>.*?)\)", re.DOTALL)
PROJECT_VERSION_PATTERN = re.compile(
    r"\bversion\s*:\s*['\"](?P<version>[^'\"]+)['\"]"
)
DEPENDENCY_PATTERN = re.compile(
    r"(?<!\.)\bdependency\s*\(\s*['\"]([^'\"]+)['\"]"
)
FALLBACK_PATTERN = re.compile(
    r"\bfallback\s*:\s*(?:\[\s*)?['\"]([^'\"]+)['\"]"
)
SUBPROJECT_PATTERN = re.compile(
    r"(?:\b|\.)subproject\s*\(\s*['\"]([^'\"]+)['\"]"
)
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
SUPPORT_DIRECTORIES = frozenset({"packagecache", "packagefiles"})
CANONICAL_ARCHIVE_PATH = pathlib.PurePosixPath("subprojects/wraps.zip")
CANONICAL_ARCHIVE_COMPRESSION = "stored"
CANONICAL_ARCHIVE_MODE = 0o644
CANONICAL_ARCHIVE_TIMESTAMP = "1980-01-01T00:00:00"
REQUIRED_MESON_EXCLUDED_ROOTS = frozenset(
    {
        ".git",
        ".install",
        ".tmp",
        "build",
        "builddir",
        "q2proto",
        "subprojects",
        "vendor",
    }
)


@dataclass
class VerificationResult:
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.errors

    def extend(self, other: "VerificationResult") -> None:
        self.errors.extend(other.errors)
        self.warnings.extend(other.warnings)


@dataclass(frozen=True)
class ArchiveSettings:
    path: pathlib.PurePosixPath
    compression: int
    entry_mode: int
    timestamp: tuple[int, int, int, int, int, int]


def _strict_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field {key!r}")
        result[key] = value
    return result


def load_baseline(path: pathlib.Path = DEFAULT_BASELINE) -> dict[str, Any]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        baseline = json.load(handle, object_pairs_hook=_strict_json_object)
    if not isinstance(baseline, dict) or baseline.get("schema_version") != 1:
        raise ValueError("dependency baseline must use schema_version 1")
    wraps = baseline.get("wraps")
    if not isinstance(wraps, dict) or not wraps:
        raise ValueError("dependency baseline has no wrap inventory")
    _validated_wrap_names(baseline)
    archive_settings(baseline)
    return baseline


def parse_wrap(path: pathlib.Path) -> dict[str, dict[str, str]]:
    parser = configparser.ConfigParser(
        interpolation=None,
        strict=True,
        empty_lines_in_values=False,
    )
    parser.optionxform = str
    try:
        with path.open("r", encoding="utf-8", newline="") as handle:
            parser.read_file(handle)
    except (OSError, UnicodeDecodeError, configparser.Error) as exc:
        raise ValueError(f"cannot parse {path.name}: {exc}") from exc
    return {
        section: dict(parser.items(section, raw=True))
        for section in parser.sections()
    }


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _safe_plain_name(value: str) -> bool:
    if not value or value in (".", "..") or "/" in value or "\\" in value:
        return False
    return pathlib.PurePath(value).name == value


def _safe_relative_path(value: str) -> bool:
    normalized = value.replace("\\", "/")
    path = pathlib.PurePosixPath(normalized)
    return bool(normalized) and not path.is_absolute() and ".." not in path.parts


def _validated_wrap_names(baseline: dict[str, Any]) -> list[str]:
    wraps = baseline.get("wraps")
    if not isinstance(wraps, dict) or not wraps:
        raise ValueError("dependency baseline has no wrap inventory")

    names: list[str] = []
    casefolded: dict[str, str] = {}
    for name in wraps:
        if not isinstance(name, str) or not _safe_plain_name(name) or not name.endswith(".wrap"):
            raise ValueError(f"baseline wrap key is not a plain .wrap filename: {name!r}")
        collision = casefolded.setdefault(name.casefold(), name)
        if collision != name:
            raise ValueError(
                f"baseline wrap keys collide case-insensitively: {collision!r}, {name!r}"
            )
        names.append(name)
    return sorted(names)


def archive_settings(baseline: dict[str, Any]) -> ArchiveSettings:
    contract = baseline.get("archive")
    if not isinstance(contract, dict):
        raise ValueError("baseline archive contract is not an object")
    expected_fields = {"path", "compression", "entry_mode", "timestamp"}
    if set(contract) != expected_fields:
        raise ValueError(
            "baseline archive contract fields differ from the schema "
            f"(actual={sorted(contract)!r})"
        )

    path_value = contract.get("path")
    if not isinstance(path_value, str) or not _safe_relative_path(path_value):
        raise ValueError("baseline archive path is not a safe repository-relative path")
    archive_path = pathlib.PurePosixPath(path_value.replace("\\", "/"))
    if archive_path != CANONICAL_ARCHIVE_PATH:
        raise ValueError(
            f"baseline archive path must be {CANONICAL_ARCHIVE_PATH.as_posix()!r}"
        )

    compression_value = contract.get("compression")
    if compression_value != CANONICAL_ARCHIVE_COMPRESSION:
        raise ValueError("baseline archive compression must be 'stored'")

    mode_value = contract.get("entry_mode")
    if not isinstance(mode_value, str) or not re.fullmatch(r"0[0-7]{3}", mode_value):
        raise ValueError("baseline archive entry_mode must be a four-digit octal string")
    entry_mode = int(mode_value, 8)
    if entry_mode != CANONICAL_ARCHIVE_MODE:
        raise ValueError("baseline archive entry_mode must be '0644'")

    timestamp_value = contract.get("timestamp")
    if timestamp_value != CANONICAL_ARCHIVE_TIMESTAMP:
        raise ValueError(
            f"baseline archive timestamp must be {CANONICAL_ARCHIVE_TIMESTAMP!r}"
        )
    try:
        timestamp = datetime.datetime.strptime(
            timestamp_value, "%Y-%m-%dT%H:%M:%S"
        ).timetuple()[:6]
    except (TypeError, ValueError) as exc:
        raise ValueError("baseline archive timestamp is invalid") from exc
    if timestamp != ZIP_TIMESTAMP:
        raise ValueError("baseline archive timestamp does not match ZIP epoch")

    return ArchiveSettings(
        path=archive_path,
        compression=zipfile.ZIP_STORED,
        entry_mode=entry_mode,
        timestamp=timestamp,
    )


def _lstat_kind(path: pathlib.Path) -> int | None:
    try:
        return path.lstat().st_mode
    except FileNotFoundError:
        return None


def _is_regular_file_no_follow(path: pathlib.Path) -> bool:
    mode = _lstat_kind(path)
    return mode is not None and stat.S_ISREG(mode)


def _is_directory_no_follow(path: pathlib.Path) -> bool:
    mode = _lstat_kind(path)
    return mode is not None and stat.S_ISDIR(mode)


def _wrap_section(sections: dict[str, dict[str, str]]) -> tuple[str, dict[str, str]]:
    candidates = [name for name in sections if name.startswith("wrap-")]
    if len(candidates) != 1:
        raise ValueError("wrap must contain exactly one wrap-* section")
    name = candidates[0]
    return name, sections[name]


def _provided_dependency_names(sections: dict[str, dict[str, str]]) -> set[str]:
    provide = sections.get("provide", {})
    names = {
        name.strip()
        for name in provide.get("dependency_names", "").split(",")
        if name.strip()
    }
    names.update(
        name
        for name in provide
        if name not in ("dependency_names", "program_names")
    )
    return names


def build_wrap_archive_bytes(
    repo_root: pathlib.Path,
    baseline: dict[str, Any],
) -> bytes:
    settings = archive_settings(baseline)
    wrap_names = _validated_wrap_names(baseline)
    output = io.BytesIO()
    subprojects = repo_root / "subprojects"
    if not _is_directory_no_follow(subprojects):
        raise ValueError("subprojects is missing or is not a regular directory")
    with zipfile.ZipFile(output, "w", compression=settings.compression) as archive:
        for name in wrap_names:
            wrap_path = subprojects / name
            if not _is_regular_file_no_follow(wrap_path):
                raise ValueError(f"subprojects/{name} is missing or is not a regular file")
            payload = wrap_path.read_bytes()
            info = zipfile.ZipInfo(name, date_time=settings.timestamp)
            info.compress_type = settings.compression
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | settings.entry_mode) << 16
            info.extra = b""
            info.comment = b""
            archive.writestr(info, payload)
    return output.getvalue()


def verify_wrap_inventory(
    repo_root: pathlib.Path,
    baseline: dict[str, Any],
) -> VerificationResult:
    result = VerificationResult()
    subprojects = repo_root / "subprojects"
    try:
        expected_wraps = set(_validated_wrap_names(baseline))
        archive_settings(baseline)
    except ValueError as exc:
        result.errors.append(str(exc))
        return result
    if not _is_directory_no_follow(subprojects):
        result.errors.append("subprojects is missing or is not a regular directory")
        return result

    actual_wraps: set[str] = set()
    try:
        entries = sorted(os.scandir(subprojects), key=lambda entry: entry.name.casefold())
    except OSError as exc:
        result.errors.append(f"cannot inventory subprojects: {exc}")
        return result
    for entry in entries:
        if not entry.name.endswith(".wrap"):
            continue
        try:
            mode = entry.stat(follow_symlinks=False).st_mode
        except OSError as exc:
            result.errors.append(f"cannot inspect subprojects/{entry.name}: {exc}")
            continue
        if not stat.S_ISREG(mode):
            kind = "symbolic link" if stat.S_ISLNK(mode) else "non-regular entry"
            result.errors.append(f"subprojects/{entry.name} is a {kind}")
            continue
        actual_wraps.add(entry.name)
    for name in sorted(expected_wraps - actual_wraps):
        result.errors.append(f"baseline wrap is missing: subprojects/{name}")
    for name in sorted(actual_wraps - expected_wraps):
        result.errors.append(f"wrap is not declared in the baseline: subprojects/{name}")

    if _lstat_kind(subprojects / ".wraplock") is not None:
        result.errors.append("transient subprojects/.wraplock must not be tracked")
    for entry in entries:
        if entry.name.endswith(".wrap.disabled"):
            result.errors.append(
                f"stale disabled wrap must be removed: subprojects/{entry.name}"
            )

    actual_sections: dict[str, dict[str, dict[str, str]]] = {}
    special_trust: dict[str, dict[str, str]] = {}
    active_directories: dict[str, str] = {}
    referenced_patch_directories: dict[str, str] = {}
    referenced_local_archives: dict[str, str] = {}

    for name in sorted(expected_wraps & actual_wraps):
        descriptor = baseline["wraps"][name]
        if not isinstance(descriptor, dict):
            result.errors.append(f"baseline descriptor for {name} is not an object")
            continue
        if set(descriptor) != {"scope", "version", "sections"}:
            result.errors.append(
                f"baseline descriptor for {name} must contain exactly "
                "scope, version, and sections"
            )
            continue
        scope = descriptor.get("scope")
        if scope not in {"direct", "transitive"}:
            result.errors.append(
                f"baseline descriptor for {name} has invalid scope {scope!r}"
            )
            continue
        version = descriptor.get("version")
        if not isinstance(version, str) or not version.strip():
            result.errors.append(
                f"baseline descriptor for {name} has no non-empty version"
            )
            continue
        expected_sections = descriptor.get("sections")
        if not isinstance(expected_sections, dict):
            result.errors.append(f"baseline descriptor for {name} has no sections")
            continue
        try:
            sections = parse_wrap(subprojects / name)
        except ValueError as exc:
            result.errors.append(str(exc))
            continue
        actual_sections[name] = sections
        if sections != expected_sections:
            result.errors.append(f"{name} metadata differs from dependency_baseline.json")
            continue

        try:
            wrap_kind, fields = _wrap_section(sections)
        except ValueError as exc:
            result.errors.append(f"{name}: {exc}")
            continue

        if wrap_kind == "wrap-file":
            directory = fields.get("directory", "")
            source_filename = fields.get("source_filename", "")
            source_hash = fields.get("source_hash", "")
            if not _safe_plain_name(directory):
                result.errors.append(f"{name}: invalid directory {directory!r}")
            else:
                previous = active_directories.setdefault(directory.casefold(), name)
                if previous != name:
                    result.errors.append(
                        f"{name}: directory {directory!r} is already owned by {previous}"
                    )
            if not _safe_plain_name(source_filename):
                result.errors.append(f"{name}: invalid source_filename {source_filename!r}")
            if not SHA256_PATTERN.fullmatch(source_hash):
                result.errors.append(f"{name}: source_hash is not a lowercase SHA-256")
            for field_name in ("source_url", "source_fallback_url", "patch_url", "patch_fallback_url"):
                value = fields.get(field_name)
                if value is not None and not value.startswith("https://"):
                    result.errors.append(f"{name}: {field_name} must use HTTPS")

            patch_filename = fields.get("patch_filename")
            patch_hash = fields.get("patch_hash")
            if (patch_filename is None) != (patch_hash is None):
                result.errors.append(f"{name}: patch_filename and patch_hash must be paired")
            if patch_filename is not None and not _safe_plain_name(patch_filename):
                result.errors.append(f"{name}: invalid patch_filename {patch_filename!r}")
            if patch_hash is not None and not SHA256_PATTERN.fullmatch(patch_hash):
                result.errors.append(f"{name}: patch_hash is not a lowercase SHA-256")

            patch_directory = fields.get("patch_directory")
            if patch_directory is not None:
                if not _safe_plain_name(patch_directory):
                    result.errors.append(f"{name}: invalid patch_directory {patch_directory!r}")
                else:
                    previous = referenced_patch_directories.setdefault(patch_directory, name)
                    if previous != name:
                        result.errors.append(
                            f"{name}: patch_directory {patch_directory!r} is already used by {previous}"
                        )

            if "source_url" not in fields:
                special_trust[name] = {
                    "kind": "repository-local-archive",
                    "sha256": source_hash,
                }
                if _safe_plain_name(source_filename):
                    referenced_local_archives[source_filename] = name
        elif wrap_kind == "wrap-git":
            url = fields.get("url", "")
            revision = fields.get("revision", "")
            if not url.startswith("https://"):
                result.errors.append(f"{name}: git URL must use HTTPS")
            active_directories[name.removesuffix(".wrap").casefold()] = name
            if not IMMUTABLE_GIT_REVISION_PATTERN.fullmatch(revision):
                special_trust[name] = {
                    "kind": "git-tag",
                    "revision": revision,
                }
        elif wrap_kind == "wrap-redirect":
            redirect = fields.get("filename", "")
            if not _safe_relative_path(redirect) or not redirect.endswith(".wrap"):
                result.errors.append(f"{name}: unsafe redirect target {redirect!r}")
            active_directories[name.removesuffix(".wrap").casefold()] = name
        else:
            result.errors.append(f"{name}: unsupported section [{wrap_kind}]")

    expected_trust = baseline.get("trust_exceptions")
    if not isinstance(expected_trust, dict):
        result.errors.append("baseline trust_exceptions is not an object")
    elif special_trust != expected_trust:
        result.errors.append("wrap trust exceptions differ from dependency_baseline.json")

    result.extend(
        verify_packagefiles(
            repo_root,
            baseline,
            actual_sections,
            referenced_patch_directories,
            referenced_local_archives,
        )
    )
    result.extend(verify_meson_contract(repo_root, baseline, actual_sections))
    result.extend(verify_wrap_archive(repo_root, baseline))
    result.warnings.extend(
        find_local_tree_drift(
            subprojects,
            {
                fields["directory"]
                if "wrap-file" in sections
                else name.removesuffix(".wrap")
                for name, sections in actual_sections.items()
                for fields in [sections.get("wrap-file", {})]
                if "wrap-file" not in sections or "directory" in fields
            },
        )
    )
    return result


def _verify_regular_tree(
    root: pathlib.Path,
    repo_root: pathlib.Path,
) -> VerificationResult:
    result = VerificationResult()
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            entries = sorted(os.scandir(directory), key=lambda entry: entry.name.casefold())
        except OSError as exc:
            result.errors.append(
                f"cannot inspect {directory.relative_to(repo_root).as_posix()}: {exc}"
            )
            continue
        for entry in entries:
            path = pathlib.Path(entry.path)
            relative = path.relative_to(repo_root).as_posix()
            try:
                mode = entry.stat(follow_symlinks=False).st_mode
            except OSError as exc:
                result.errors.append(f"cannot inspect {relative}: {exc}")
                continue
            if stat.S_ISLNK(mode):
                result.errors.append(f"package override contains a symbolic link: {relative}")
            elif stat.S_ISDIR(mode):
                pending.append(path)
            elif not stat.S_ISREG(mode):
                result.errors.append(f"package override contains a special entry: {relative}")
    return result


def verify_packagefiles(
    repo_root: pathlib.Path,
    baseline: dict[str, Any],
    actual_sections: dict[str, dict[str, dict[str, str]]],
    referenced_patch_directories: dict[str, str],
    referenced_local_archives: dict[str, str],
) -> VerificationResult:
    result = VerificationResult()
    packagefiles = repo_root / "subprojects" / "packagefiles"
    if not _is_directory_no_follow(packagefiles):
        result.errors.append("subprojects/packagefiles is missing or is not a regular directory")
        return result

    actual_directories: set[str] = set()
    actual_files: set[str] = set()
    try:
        entries = sorted(os.scandir(packagefiles), key=lambda entry: entry.name.casefold())
    except OSError as exc:
        result.errors.append(f"cannot inventory subprojects/packagefiles: {exc}")
        return result
    for entry in entries:
        relative = f"packagefiles/{entry.name}"
        try:
            mode = entry.stat(follow_symlinks=False).st_mode
        except OSError as exc:
            result.errors.append(f"cannot inspect {relative}: {exc}")
            continue
        if stat.S_ISLNK(mode):
            result.errors.append(f"packagefiles root entry is a symbolic link: {relative}")
        elif stat.S_ISDIR(mode):
            actual_directories.add(entry.name)
        elif stat.S_ISREG(mode):
            actual_files.add(entry.name)
        else:
            result.errors.append(f"packagefiles root entry is special: {relative}")

    expected_directories = set(referenced_patch_directories)
    for name in sorted(expected_directories - actual_directories):
        result.errors.append(f"referenced package override is missing: packagefiles/{name}")
    for name in sorted(actual_directories - expected_directories):
        directory = packagefiles / name
        if any(directory.iterdir()):
            result.errors.append(
                f"unreferenced package override directory: packagefiles/{name}"
            )

    expected_files = set(referenced_local_archives)
    for name in sorted(expected_files - actual_files):
        result.errors.append(f"repository-local source archive is missing: packagefiles/{name}")
    for name in sorted(actual_files - expected_files):
        result.errors.append(f"unreferenced packagefiles artifact: packagefiles/{name}")

    for filename, wrap_name in sorted(referenced_local_archives.items()):
        path = packagefiles / filename
        if not _is_regular_file_no_follow(path):
            continue
        expected_hash = actual_sections[wrap_name]["wrap-file"]["source_hash"]
        try:
            actual_hash = sha256_file(path)
        except OSError as exc:
            result.errors.append(f"cannot hash packagefiles/{filename}: {exc}")
            continue
        if actual_hash != expected_hash:
            result.errors.append(
                f"packagefiles/{filename} SHA-256 mismatch ({actual_hash} != {expected_hash})"
            )

    for directory, wrap_name in sorted(referenced_patch_directories.items()):
        root = packagefiles / directory
        if not _is_directory_no_follow(root):
            continue
        result.extend(_verify_regular_tree(root, repo_root))
        meson_file = root / "meson.build"
        if not _is_regular_file_no_follow(meson_file):
            result.errors.append(f"packagefiles/{directory} has no regular meson.build")
            continue
        try:
            text = meson_file.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            result.errors.append(f"cannot read packagefiles/{directory}/meson.build: {exc}")
            continue
        project_match = PROJECT_PATTERN.search(text)
        version_match = (
            PROJECT_VERSION_PATTERN.search(project_match.group("body"))
            if project_match
            else None
        )
        expected_version = baseline["wraps"][wrap_name]["version"]
        actual_version = version_match.group("version") if version_match else None
        if actual_version != expected_version:
            result.errors.append(
                f"packagefiles/{directory}/meson.build version mismatch "
                f"({actual_version!r} != {expected_version!r})"
            )
    return result


def _meson_without_comments(text: str) -> str:
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def _validated_string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError(f"baseline {label} must be a list of strings")
    if len(value) != len(set(value)):
        raise ValueError(f"baseline {label} contains duplicate entries")
    if value != sorted(value):
        raise ValueError(f"baseline {label} must be sorted")
    return value


def _repo_path(repo_root: pathlib.Path, relative: str) -> pathlib.Path:
    normalized = pathlib.PurePosixPath(relative.replace("\\", "/"))
    return repo_root.joinpath(*normalized.parts)


def _discover_meson_files(
    repo_root: pathlib.Path,
    root_files: list[str],
    scan_roots: list[str],
) -> tuple[dict[str, pathlib.Path], VerificationResult]:
    result = VerificationResult()
    discovered: dict[str, pathlib.Path] = {}

    for relative in root_files:
        path = _repo_path(repo_root, relative)
        if not _is_regular_file_no_follow(path):
            result.errors.append(f"Meson root contract file is missing or non-regular: {relative}")
            continue
        discovered[relative] = path

    for relative_root in scan_roots:
        scan_root = _repo_path(repo_root, relative_root)
        if not _is_directory_no_follow(scan_root):
            result.errors.append(
                f"Meson scan root is missing or is not a regular directory: {relative_root}"
            )
            continue
        pending = [scan_root]
        while pending:
            directory = pending.pop()
            try:
                entries = sorted(
                    os.scandir(directory), key=lambda entry: entry.name.casefold()
                )
            except OSError as exc:
                result.errors.append(
                    f"cannot scan Meson directory "
                    f"{directory.relative_to(repo_root).as_posix()}: {exc}"
                )
                continue
            for entry in entries:
                path = pathlib.Path(entry.path)
                relative = path.relative_to(repo_root).as_posix()
                try:
                    mode = entry.stat(follow_symlinks=False).st_mode
                except OSError as exc:
                    result.errors.append(f"cannot inspect Meson scan entry {relative}: {exc}")
                    continue
                if stat.S_ISLNK(mode):
                    result.errors.append(f"Meson scan root contains a symbolic link: {relative}")
                    continue
                if stat.S_ISDIR(mode):
                    pending.append(path)
                    continue
                if entry.name != "meson.build":
                    continue
                if not stat.S_ISREG(mode):
                    result.errors.append(f"Meson build entry is not a regular file: {relative}")
                    continue
                discovered[relative] = path
    return dict(sorted(discovered.items())), result


def _meson_consumer_record(path: pathlib.Path) -> dict[str, list[str]]:
    text = _meson_without_comments(path.read_text(encoding="utf-8"))
    return {
        "dependencies": sorted(set(DEPENDENCY_PATTERN.findall(text))),
        "fallbacks": sorted(set(FALLBACK_PATTERN.findall(text))),
        "subprojects": sorted(set(SUBPROJECT_PATTERN.findall(text))),
    }


def verify_meson_contract(
    repo_root: pathlib.Path,
    baseline: dict[str, Any],
    actual_sections: dict[str, dict[str, dict[str, str]]],
) -> VerificationResult:
    result = VerificationResult()
    contract = baseline.get("meson")
    if not isinstance(contract, dict):
        result.errors.append("baseline meson contract is not an object")
        return result

    try:
        root_files = _validated_string_list(contract.get("root_files"), "meson.root_files")
        scan_roots = _validated_string_list(contract.get("scan_roots"), "meson.scan_roots")
        excluded_roots = _validated_string_list(
            contract.get("excluded_roots"), "meson.excluded_roots"
        )
        for relative in root_files:
            if not _safe_relative_path(relative) or pathlib.PurePosixPath(relative).name != "meson.build":
                raise ValueError(f"baseline Meson root file is unsafe or not meson.build: {relative!r}")
        for relative in [*scan_roots, *excluded_roots]:
            if not _safe_plain_name(relative):
                raise ValueError(f"baseline Meson root name is not plain: {relative!r}")
        if set(scan_roots) & set(excluded_roots):
            raise ValueError("baseline Meson scan roots overlap excluded roots")
        missing_exclusions = REQUIRED_MESON_EXCLUDED_ROOTS - set(excluded_roots)
        if missing_exclusions:
            raise ValueError(
                "baseline Meson exclusions omit generated/vendor roots: "
                f"{sorted(missing_exclusions)!r}"
            )
    except ValueError as exc:
        result.errors.append(str(exc))
        return result

    expected_consumer_files = contract.get("consumer_files")
    if not isinstance(expected_consumer_files, dict):
        result.errors.append("baseline meson.consumer_files is not an object")
        return result
    normalized_expected: dict[str, dict[str, list[str]]] = {}
    allowed_roots = set(root_files)
    for relative, record in expected_consumer_files.items():
        if not isinstance(relative, str) or not _safe_relative_path(relative):
            result.errors.append(f"baseline Meson consumer path is unsafe: {relative!r}")
            continue
        parts = pathlib.PurePosixPath(relative.replace("\\", "/")).parts
        if relative not in allowed_roots and (not parts or parts[0] not in scan_roots):
            result.errors.append(
                f"baseline Meson consumer is outside bounded scan roots: {relative}"
            )
            continue
        if not isinstance(record, dict) or set(record) != {
            "dependencies",
            "fallbacks",
            "subprojects",
        }:
            result.errors.append(
                f"baseline Meson consumer record has invalid fields: {relative}"
            )
            continue
        try:
            normalized_expected[relative] = {
                key: _validated_string_list(
                    record[key], f"meson.consumer_files[{relative!r}].{key}"
                )
                for key in ("dependencies", "fallbacks", "subprojects")
            }
        except ValueError as exc:
            result.errors.append(str(exc))

    discovered, discovery_result = _discover_meson_files(
        repo_root, root_files, scan_roots
    )
    result.extend(discovery_result)
    actual_consumer_files: dict[str, dict[str, list[str]]] = {}
    for relative, path in discovered.items():
        try:
            record = _meson_consumer_record(path)
        except (OSError, UnicodeDecodeError) as exc:
            result.errors.append(f"cannot read Meson contract file {relative}: {exc}")
            continue
        if any(record.values()):
            actual_consumer_files[relative] = record

    if actual_consumer_files != normalized_expected:
        result.errors.append(
            "Meson consumer-bearing file inventory differs from the baseline "
            f"(actual={actual_consumer_files!r})"
        )

    dependencies = {
        item
        for record in actual_consumer_files.values()
        for item in record["dependencies"]
    }
    fallbacks = {
        item
        for record in actual_consumer_files.values()
        for item in record["fallbacks"]
    }
    subprojects = {
        item
        for record in actual_consumer_files.values()
        for item in record["subprojects"]
    }
    dependency_consumers = contract.get("dependency_consumers", {})
    fallback_consumers = contract.get("fallback_consumers", {})
    subproject_consumers = contract.get("subproject_consumers", {})
    try:
        system_dependencies = set(
            _validated_string_list(
                contract.get("system_dependencies"), "meson.system_dependencies"
            )
        )
    except ValueError as exc:
        result.errors.append(str(exc))
        system_dependencies = set()

    consumer_maps_are_valid = True
    for consumer_map_name, consumer_map in (
        ("dependency_consumers", dependency_consumers),
        ("fallback_consumers", fallback_consumers),
        ("subproject_consumers", subproject_consumers),
    ):
        if not isinstance(consumer_map, dict) or not all(
            isinstance(name, str) and isinstance(wrap_name, str)
            for name, wrap_name in getattr(consumer_map, "items", lambda: ())()
        ):
            result.errors.append(f"baseline {consumer_map_name} is not a string map")
            consumer_maps_are_valid = False

    if not consumer_maps_are_valid:
        return result

    if dependencies != set(dependency_consumers) | system_dependencies:
        result.errors.append(
            "Meson dependency declarations differ from the baseline "
            f"(actual={sorted(dependencies)!r})"
        )
    if fallbacks != set(fallback_consumers):
        result.errors.append(
            "Meson fallback declarations differ from the baseline "
            f"(actual={sorted(fallbacks)!r})"
        )
    if subprojects != set(subproject_consumers):
        result.errors.append(
            "Meson subproject declarations differ from the baseline "
            f"(actual={sorted(subprojects)!r})"
        )

    for consumer_map_name, consumer_map in (
        ("dependency_consumers", dependency_consumers),
        ("fallback_consumers", fallback_consumers),
        ("subproject_consumers", subproject_consumers),
    ):
        for consumer, wrap_name in consumer_map.items():
            if wrap_name not in actual_sections:
                result.errors.append(
                    f"Meson consumer {consumer!r} references missing baseline wrap {wrap_name}"
                )
                continue
            if consumer_map_name == "dependency_consumers":
                provided = _provided_dependency_names(actual_sections[wrap_name])
                explicit_fallback_wraps = set(fallback_consumers.values())
                if consumer not in provided and wrap_name not in explicit_fallback_wraps:
                    result.errors.append(
                        f"{wrap_name} does not provide dependency {consumer!r}"
                    )
    return result


def verify_wrap_archive(
    repo_root: pathlib.Path,
    baseline: dict[str, Any],
) -> VerificationResult:
    result = VerificationResult()
    try:
        settings = archive_settings(baseline)
    except ValueError as exc:
        result.errors.append(str(exc))
        return result
    archive_path = _repo_path(repo_root, settings.path.as_posix())
    if not _is_regular_file_no_follow(archive_path):
        result.errors.append("deterministic wraps archive is missing or not a regular file")
        return result
    try:
        expected = build_wrap_archive_bytes(repo_root, baseline)
        actual = archive_path.read_bytes()
    except (OSError, KeyError, ValueError, zipfile.BadZipFile) as exc:
        result.errors.append(f"cannot validate wraps archive: {exc}")
        return result
    if actual != expected:
        try:
            with zipfile.ZipFile(io.BytesIO(actual)) as archive:
                actual_names = archive.namelist()
        except zipfile.BadZipFile:
            actual_names = ["<invalid zip>"]
        result.errors.append(
            f"{settings.path.as_posix()} is not the exact deterministic baseline archive "
            f"(entries={actual_names!r}); run rebuild_wraps_archive.py"
        )
    return result


def _directory_family(name: str) -> str:
    match = VERSIONED_DIRECTORY_PATTERN.fullmatch(name)
    return (match.group("family") if match else name).casefold()


def find_local_tree_drift(
    subprojects: pathlib.Path,
    active_directories: set[str],
) -> list[str]:
    active_by_family: dict[str, set[str]] = {}
    for name in active_directories:
        active_by_family.setdefault(_directory_family(name), set()).add(name.casefold())

    observed_by_family: dict[str, list[str]] = {}
    for path in subprojects.iterdir():
        if not path.is_dir() or path.is_symlink() or path.name in SUPPORT_DIRECTORIES:
            continue
        observed_by_family.setdefault(_directory_family(path.name), []).append(path.name)

    warnings: list[str] = []
    for family, names in sorted(observed_by_family.items()):
        names = sorted(names, key=str.casefold)
        active_names = active_by_family.get(family)
        if active_names is None:
            if any(VERSIONED_DIRECTORY_PATTERN.fullmatch(name) for name in names):
                warnings.append(
                    f"unmanaged versioned local subproject tree: {', '.join(names)}"
                )
            continue
        stale = [name for name in names if name.casefold() not in active_names]
        if stale:
            warnings.append(
                f"stale local subproject tree for {family}: {', '.join(stale)}"
            )
        if len(names) > 1:
            warnings.append(
                f"duplicate local subproject trees for {family}: {', '.join(names)}"
            )
    return warnings


def verify_repository(
    repo_root: pathlib.Path = REPO_ROOT,
    baseline_path: pathlib.Path = DEFAULT_BASELINE,
) -> VerificationResult:
    try:
        baseline = load_baseline(baseline_path)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        return VerificationResult(errors=[f"cannot load dependency baseline: {exc}"])
    return verify_wrap_inventory(repo_root.resolve(), baseline)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify WORR's repository-local Meson dependency baseline."
    )
    parser.add_argument("--repo-root", default=str(REPO_ROOT))
    parser.add_argument("--baseline", default=str(DEFAULT_BASELINE))
    parser.add_argument(
        "--strict-local-trees",
        action="store_true",
        help="Treat ignored stale/duplicate local checkout warnings as failures.",
    )
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args()

    result = verify_repository(
        pathlib.Path(args.repo_root),
        pathlib.Path(args.baseline),
    )
    if args.strict_local_trees and result.warnings:
        result.errors.extend(f"local tree drift: {warning}" for warning in result.warnings)

    if args.format == "json":
        print(
            json.dumps(
                {
                    "ok": result.ok,
                    "errors": result.errors,
                    "warnings": result.warnings,
                },
                indent=2,
                sort_keys=True,
            )
        )
    else:
        for warning in result.warnings:
            print(f"warning: {warning}")
        for error in result.errors:
            print(f"error: {error}")
        if result.ok:
            print(
                "Dependency baseline verified: wraps, archive, package overrides, "
                "local sources, and Meson declarations are consistent."
            )
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
