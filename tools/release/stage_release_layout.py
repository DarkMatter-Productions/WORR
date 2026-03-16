#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import shutil


def copy_unique(source: pathlib.Path, dest: pathlib.Path) -> None:
    if dest.exists():
        raise SystemExit(f"Release layout collision: {dest}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)


def merge_tree(source_root: pathlib.Path, dest_root: pathlib.Path) -> int:
    if not source_root.is_dir():
        return 0

    copied = 0
    for source in sorted(path for path in source_root.rglob("*") if path.is_file()):
        rel_path = source.relative_to(source_root)
        copy_unique(source, dest_root / rel_path)
        copied += 1
    return copied


def unique_merge_sources(paths: list[pathlib.Path]) -> list[pathlib.Path]:
    unique_paths: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for path in paths:
        resolved = path.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
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
        if entry.is_dir() and entry.name in {".release", base_game, release_game}:
            continue

        if entry.is_file() and entry.name in relocate_root_files:
            target = output_dir / relocate_root_dir / entry.name
        else:
            target = output_dir / entry.name

        if entry.is_dir():
            if target.exists():
                raise SystemExit(f"Release layout collision: {target}")
            shutil.copytree(entry, target)
            copied += sum(1 for path in target.rglob("*") if path.is_file())
        elif entry.is_file():
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
    if not input_dir.is_dir():
        raise SystemExit(f"Input directory not found: {input_dir}")

    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    relocate_root_file_set = set(relocate_root_files or [])
    copy_root_entries(
        input_dir,
        output_dir,
        base_game,
        release_game,
        relocate_root_file_set,
        relocate_root_dir,
    )

    release_game_dir = output_dir / release_game
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

    input_dir = pathlib.Path(args.input_dir).resolve()
    output_dir = pathlib.Path(args.output_dir).resolve()
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
