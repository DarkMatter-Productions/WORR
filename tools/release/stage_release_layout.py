#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import stat
import sys
import tempfile

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.atomic_directory import publish_staged_directory_atomically
from tools.package_release import validated_archive_filename, validated_archive_path


def is_link_like(status: os.stat_result) -> bool:
    return stat.S_ISLNK(status.st_mode) or bool(
        getattr(status, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    )


def source_kind(path: pathlib.Path) -> str:
    try:
        status = path.lstat()
    except OSError as exc:
        raise SystemExit(f"Cannot inspect release source {path}: {exc}") from exc
    if is_link_like(status):
        raise SystemExit(f"Release source links/reparse points are not allowed: {path}")
    if stat.S_ISREG(status.st_mode):
        return "file"
    if stat.S_ISDIR(status.st_mode):
        return "directory"
    raise SystemExit(f"Release source is not a regular file or directory: {path}")


def regular_tree_files(source_root: pathlib.Path) -> list[pathlib.Path]:
    if not source_root.exists() and not source_root.is_symlink():
        return []
    if source_kind(source_root) != "directory":
        raise SystemExit(f"Release tree source is not a directory: {source_root}")

    files: list[pathlib.Path] = []
    pending = [source_root]
    while pending:
        directory = pending.pop()
        try:
            entries = sorted(os.scandir(directory), key=lambda entry: entry.name)
        except OSError as exc:
            raise SystemExit(f"Cannot scan release source {directory}: {exc}") from exc
        for entry in entries:
            path = pathlib.Path(entry.path)
            kind = source_kind(path)
            if kind == "directory":
                pending.append(path)
            else:
                files.append(path)
    return sorted(files)


def copy_unique(source: pathlib.Path, dest: pathlib.Path) -> None:
    if source_kind(source) != "file":
        raise SystemExit(f"Release source is not a regular file: {source}")
    if dest.exists() or dest.is_symlink():
        raise SystemExit(f"Release layout collision: {dest}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest, follow_symlinks=False)


def merge_tree(source_root: pathlib.Path, dest_root: pathlib.Path) -> int:
    copied = 0
    for source in regular_tree_files(source_root):
        rel_path = source.relative_to(source_root)
        copy_unique(source, dest_root / rel_path)
        copied += 1
    return copied


def unique_merge_sources(paths: list[pathlib.Path]) -> list[pathlib.Path]:
    unique_paths: list[pathlib.Path] = []
    seen: set[str] = set()
    for path in paths:
        lexical_key = os.path.normcase(str(path.absolute()))
        if lexical_key in seen:
            continue
        seen.add(lexical_key)
        unique_paths.append(path)
    return unique_paths


def copy_root_entries(
    input_dir: pathlib.Path,
    output_dir: pathlib.Path,
    base_game: str,
    release_game: str,
    relocate_root_files: set[str],
    relocate_root_dir: str,
) -> int:
    copied = 0
    for entry in sorted(input_dir.iterdir()):
        kind = source_kind(entry)
        if kind == "directory" and entry.name in {".release", base_game, release_game}:
            continue

        if kind == "file" and entry.name in relocate_root_files:
            target = output_dir / relocate_root_dir / entry.name
        else:
            target = output_dir / entry.name

        if kind == "directory":
            if target.exists():
                raise SystemExit(f"Release layout collision: {target}")
            copied += merge_tree(entry, target)
        else:
            copy_unique(entry, target)
            copied += 1
    return copied


def stage_release_layout(
    input_dir: pathlib.Path,
    output_dir: pathlib.Path,
    *,
    base_game: str = "basew",
    release_game: str = "basew",
    relocate_root_files: list[str] | tuple[str, ...] | None = None,
    relocate_root_dir: str = "bin",
) -> None:
    base_game = validated_archive_filename(base_game, "base game directory")
    release_game = validated_archive_filename(release_game, "release game directory")
    relocate_root_dir = validated_archive_path(relocate_root_dir, "root relocation directory")

    raw_input = pathlib.Path(input_dir)
    raw_output = pathlib.Path(output_dir)
    if source_kind(raw_input) != "directory":
        raise SystemExit(f"Input directory not found: {raw_input}")
    if (raw_output.exists() or raw_output.is_symlink()) and source_kind(raw_output) != "directory":
        raise SystemExit(f"Release output is not a directory: {raw_output}")

    input_dir = raw_input.resolve(strict=True)
    output_dir = raw_output.resolve(strict=False)
    if input_dir == output_dir or input_dir in output_dir.parents or output_dir in input_dir.parents:
        raise SystemExit(f"Release staging input/output must not overlap: {input_dir} -> {output_dir}")

    # Validate the complete source tree before replacing a previous output.
    # Copy operations repeat the no-follow checks to reduce the race window.
    regular_tree_files(input_dir)

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f".{output_dir.name}.stage.",
        dir=output_dir.parent,
    ) as temp_dir:
        staged_output = pathlib.Path(temp_dir) / "release-layout"
        staged_output.mkdir()

        relocate_root_file_set = set(relocate_root_files or [])
        copy_root_entries(
            input_dir,
            staged_output,
            base_game,
            release_game,
            relocate_root_file_set,
            relocate_root_dir,
        )

        release_game_dir = staged_output / release_game
        if release_game_dir.exists() and not release_game_dir.is_dir():
            raise SystemExit(
                f"Release gamedir path is blocked by a file: {release_game_dir}. "
                f"Use --relocate-root-file for colliding executables."
            )

        copied = 0
        for source_root in unique_merge_sources([
            input_dir / base_game,
            input_dir / release_game,
            input_dir / ".release" / release_game,
        ]):
            copied += merge_tree(source_root, release_game_dir)

        if copied == 0:
            raise SystemExit(
                f"No release gamedir files found while staging {input_dir} into {output_dir}"
            )

        publish_staged_directory_atomically(staged_output, output_dir)


def main() -> int:
    parser = argparse.ArgumentParser(description="Stage the published release layout from a .install tree.")
    parser.add_argument("--input-dir", required=True, help="Input staging directory (usually .install)")
    parser.add_argument("--output-dir", required=True, help="Output directory for the release-shaped tree")
    parser.add_argument("--base-game", default="basew", help="Local runtime game directory name")
    parser.add_argument("--release-game", default="basew", help="Published release game directory name")
    parser.add_argument(
        "--relocate-root-file",
        action="append",
        default=[],
        help="Move a conflicting root-level file into --relocate-root-dir before merging the release gamedir",
    )
    parser.add_argument(
        "--relocate-root-dir",
        default="bin",
        help="Directory used for files relocated by --relocate-root-file",
    )
    args = parser.parse_args()

    input_dir = pathlib.Path(args.input_dir)
    output_dir = pathlib.Path(args.output_dir)
    stage_release_layout(
        input_dir,
        output_dir,
        base_game=args.base_game,
        release_game=args.release_game,
        relocate_root_files=args.relocate_root_file,
        relocate_root_dir=args.relocate_root_dir,
    )
    print(f"Staged release layout: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
