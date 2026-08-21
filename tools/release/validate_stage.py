#!/usr/bin/env python3

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import os
import pathlib
import stat
import sys
import zipfile

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.release.targets import RUNTIME_COORDINATION_LOCK, get_target


ROLE_GENERATED_STAGE_PATHS = frozenset({"worr_update.json"})


def require_file(path: pathlib.Path) -> None:
    if not path.is_file():
        raise SystemExit(f"Missing required file: {path}")


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def normalize_archive_member(member: str) -> str:
    normalized = member.strip().replace("\\", "/")
    parts = pathlib.PurePosixPath(normalized).parts
    if (
        not normalized
        or normalized.endswith("/")
        or normalized.startswith("/")
        or any(part in ("", ".", "..") for part in parts)
        or (parts and len(parts[0]) == 2 and parts[0][1] == ":")
    ):
        raise SystemExit(f"Invalid archive member requirement: {member!r}")
    return normalized


def parse_required_archive_member(value: str) -> tuple[str, str | None]:
    member, separator, expected_hash = value.partition("=")
    normalized_member = normalize_archive_member(member)
    if not separator:
        return normalized_member, None

    expected_hash = expected_hash.strip().lower()
    if len(expected_hash) != 64 or any(character not in "0123456789abcdef" for character in expected_hash):
        raise SystemExit(
            "Invalid archive member SHA-256 requirement for "
            f"{normalized_member}: expected 64 lowercase/uppercase hex characters"
        )
    return normalized_member, expected_hash


def require_archive_members(archive_path: pathlib.Path, requirements: list[tuple[str, str | None]]) -> None:
    if not requirements:
        return

    try:
        with zipfile.ZipFile(archive_path) as archive:
            members: dict[str, zipfile.ZipInfo] = {}
            for info in archive.infolist():
                member_name = info.filename.replace("\\", "/")
                if member_name.endswith("/"):
                    continue
                if member_name in members:
                    raise SystemExit(f"Duplicate archive member in {archive_path}: {member_name}")
                members[member_name] = info

            for member_name, expected_hash in requirements:
                info = members.get(member_name)
                if info is None:
                    raise SystemExit(f"Missing required archive member: {archive_path}!{member_name}")
                if expected_hash is None:
                    continue

                payload = archive.read(info)
                actual_hash = sha256_bytes(payload)
                if actual_hash.lower() != expected_hash.lower():
                    raise SystemExit(
                        f"Archive member hash mismatch for {archive_path}!{member_name}: "
                        f"expected {expected_hash.lower()}, got {actual_hash.lower()}"
                    )
    except zipfile.BadZipFile as exc:
        raise SystemExit(f"Package archive is not a readable zip/pkz file: {archive_path}: {exc}") from exc
    except OSError as exc:
        raise SystemExit(f"Unable to read package archive {archive_path}: {exc}") from exc


def staged_launch_exe(config: dict[str, str]) -> str:
    return config.get("staged_launch_exe", config["launch_exe"])


def _is_link_or_reparse(path: pathlib.Path) -> bool:
    metadata = path.lstat()
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return stat.S_ISLNK(metadata.st_mode) or bool(
        getattr(metadata, "st_file_attributes", 0) & reparse_flag
    )


def validate_exact_stage_contract(
    install_dir: pathlib.Path,
    target: dict[str, object],
) -> None:
    entries: dict[str, pathlib.Path] = {}
    files: dict[str, pathlib.Path] = {}
    portable_identities: dict[str, str] = {}
    for path in install_dir.rglob("*"):
        relative = path.relative_to(install_dir).as_posix()
        if _is_link_or_reparse(path):
            raise SystemExit(f"Staged payload contains a link or reparse point: {relative}")
        identity = relative.casefold()
        prior = portable_identities.get(identity)
        if prior is not None:
            raise SystemExit(
                f"Staged payload contains colliding portable paths: {prior} and {relative}"
            )
        portable_identities[identity] = relative
        entries[relative] = path
        if path.is_file():
            files[relative] = path

    client = target["client"]
    if not isinstance(client, dict):
        raise SystemExit("Release target has an invalid client contract")
    required_paths = client.get("update_required_paths", [])
    forbidden_paths = client.get("update_forbidden_paths", [])
    if not isinstance(required_paths, list) or not isinstance(forbidden_paths, list):
        raise SystemExit("Release target has invalid staged path requirements")

    file_keys = {relative.casefold() for relative in files}
    for required in required_paths:
        if isinstance(required, str) and required in ROLE_GENERATED_STAGE_PATHS:
            # prepare_role_install.py creates this channel/role-specific file
            # from the clean shared .install input after the PR stage gate.
            continue
        if not isinstance(required, str) or required.casefold() not in file_keys:
            raise SystemExit(f"Missing required staged release file: {required}")

    for pattern in forbidden_paths:
        if not isinstance(pattern, str):
            raise SystemExit("Release target contains an invalid forbidden-path pattern")
        normalized_pattern = pattern.casefold()
        hit = next(
            (
                relative
                for relative in sorted(entries)
                if fnmatch.fnmatchcase(relative.casefold(), normalized_pattern)
            ),
            None,
        )
        if hit is not None:
            raise SystemExit(
                f"Staged payload contains forbidden runtime/user data: {hit} "
                f"(matched {pattern})"
            )

    runtime_lock = files.get(RUNTIME_COORDINATION_LOCK)
    if runtime_lock is None or runtime_lock.stat().st_size != 0:
        raise SystemExit(
            f"Stable runtime coordination lock must be an empty file: "
            f"{RUNTIME_COORDINATION_LOCK}"
        )
    if os.name != "nt" and stat.S_IMODE(runtime_lock.stat().st_mode) != 0o644:
        raise SystemExit(
            f"Stable runtime coordination lock must have mode 0644: "
            f"{RUNTIME_COORDINATION_LOCK}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate staged release contents for a release target.")
    parser.add_argument("--install-dir", required=True, help="Staged install directory (.install)")
    parser.add_argument("--base-game", default="basew", help="Base game directory name")
    parser.add_argument("--archive-name", default="pak0.pkz", help="Expected base game asset archive")
    parser.add_argument("--platform-id", required=True, help="Release platform id")
    parser.add_argument(
        "--required-archive-member",
        action="append",
        default=[],
        metavar="MEMBER[=SHA256]",
        help="Require a member inside the base-game archive, optionally with a SHA-256 hash.",
    )
    args = parser.parse_args()

    install_dir = pathlib.Path(args.install_dir).resolve()
    if not install_dir.is_dir():
        raise SystemExit(f"Install directory not found: {install_dir}")

    target = get_target(args.platform_id)
    validate_exact_stage_contract(install_dir, target)

    client_launcher = install_dir / staged_launch_exe(target["client"])
    client_engine = install_dir / target["client"]["engine_library"]
    server_launcher = install_dir / staged_launch_exe(target["server"])
    server_engine = install_dir / target["server"]["engine_library"]

    require_file(client_launcher)
    require_file(client_engine)
    require_file(server_launcher)
    require_file(server_engine)

    base_game_dir = install_dir / args.base_game
    if not base_game_dir.is_dir():
        raise SystemExit(f"Missing base game directory: {base_game_dir}")
    archive_path = base_game_dir / args.archive_name
    require_file(archive_path)
    require_archive_members(
        archive_path,
        [parse_required_archive_member(value) for value in args.required_archive_member],
    )

    base_files = list(base_game_dir.rglob("*"))
    base_files = [path for path in base_files if path.is_file()]
    if not base_files:
        raise SystemExit(f"Base game directory is empty: {base_game_dir}")

    updater = target.get("autoupdater", {})
    updater_asset = updater.get("updater_asset")
    if updater_asset:
        require_file(install_dir / updater_asset)

    print(f"Validated staged install for {args.platform_id}: {install_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as exc:  # pragma: no cover
        print(f"Validation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
