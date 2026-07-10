#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil


RUNTIME_EXTENSIONS = {"", ".exe", ".dll", ".pdb", ".so", ".dylib"}
RUNTIME_PATTERNS = ('worr_*', 'worr*.exe', 'worr*.dll', 'worr*.pdb', 'worr*.so', 'worr*.dylib')
LEGACY_RUNTIME_PATTERNS = ('worr_runtime_*', 'worr_ded_runtime_*')
RUNTIME_DEPENDENCIES = ('rmlui_core.dll',)
GENERATED_BASE_GAME_PATTERNS = ('cgame*', 'sgame*', 'shader_vkpt', 'pak0.pkz')
Q2AAS_REFERENCE_MAP_DIR = pathlib.Path("tools") / "q2aas" / "reference_maps"


def is_runtime_file(path: pathlib.Path) -> bool:
    return path.is_file() and path.suffix.lower() in RUNTIME_EXTENSIONS


def should_stage_runtime_file(path: pathlib.Path) -> bool:
    return (
        is_runtime_file(path)
        and path.name.startswith("worr")
        and "_runtime_" not in path.name
        and "_launcher_" not in path.name
    )


def remove_path(path: pathlib.Path) -> None:
    if not path.exists() and not path.is_symlink():
        return
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    else:
        path.unlink()


def clear_globbed_paths(root: pathlib.Path, patterns: tuple[str, ...]) -> None:
    for pattern in patterns:
        for path in sorted(root.glob(pattern)):
            remove_path(path)


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def copy_runtime_files(build_dir: pathlib.Path, install_dir: pathlib.Path) -> int:
    copied = 0
    copied_paths: set[pathlib.Path] = set()

    clear_globbed_paths(install_dir, RUNTIME_PATTERNS + LEGACY_RUNTIME_PATTERNS)
    remove_path(install_dir / "bin")
    for pattern in RUNTIME_PATTERNS:
        for path in sorted(build_dir.glob(pattern)):
            if not should_stage_runtime_file(path) or path in copied_paths:
                continue
            dest = install_dir / path.name
            remove_path(dest)
            shutil.copy2(path, dest)
            if file_sha256(path) != file_sha256(dest):
                raise SystemExit(f'Runtime staging verification failed for {path} -> {dest}')
            copied_paths.add(path)
            copied += 1
    return copied


def copy_runtime_dependencies(build_dir: pathlib.Path, install_dir: pathlib.Path) -> int:
    copied = 0

    for dependency_name in RUNTIME_DEPENDENCIES:
        dest = install_dir / dependency_name
        remove_path(dest)

        candidates = [build_dir / dependency_name]
        candidates.extend(sorted(build_dir.glob(f"**/{dependency_name}")))
        source = next((path for path in candidates if path.is_file()), None)
        if not source:
            continue

        shutil.copy2(source, dest)
        if file_sha256(source) != file_sha256(dest):
            raise SystemExit(f'Runtime dependency staging verification failed for {source} -> {dest}')
        copied += 1

    return copied


def copy_base_game_tree(build_dir: pathlib.Path, install_dir: pathlib.Path, base_game: str) -> None:
    source = build_dir / base_game
    target = install_dir / base_game
    if not source.is_dir():
        raise SystemExit(f'Expected staged game directory not found: {source}')
    target.mkdir(parents=True, exist_ok=True)
    clear_globbed_paths(target, GENERATED_BASE_GAME_PATTERNS)
    shutil.copytree(source, target, dirs_exist_ok=True)

    # Meson links game modules in the build root; the generated base-game tree can
    # lag behind when a single module is rebuilt. Overlay the root artifacts last.
    for pattern in ("cgame*", "sgame*"):
        for path in sorted(build_dir.glob(pattern)):
            if not is_runtime_file(path):
                continue
            dest = target / path.name
            remove_path(dest)
            shutil.copy2(path, dest)
            if file_sha256(path) != file_sha256(dest):
                raise SystemExit(f'Game module staging verification failed for {path} -> {dest}')


def stage_q2aas_reference_maps(repo_root: pathlib.Path, install_dir: pathlib.Path, base_game: str) -> int:
    source_dir = repo_root / Q2AAS_REFERENCE_MAP_DIR
    if not source_dir.is_dir():
        return 0

    maps_dir = install_dir / base_game / "maps"
    maps_dir.mkdir(parents=True, exist_ok=True)
    copied = 0
    for source in sorted(source_dir.glob("*.bsp")):
        dest = maps_dir / source.name
        remove_path(dest)
        shutil.copy2(source, dest)
        if file_sha256(source) != file_sha256(dest):
            raise SystemExit(f'q2aas reference map staging verification failed for {source} -> {dest}')
        copied += 1
    return copied


def main() -> int:
    parser = argparse.ArgumentParser(description='Stage WORR runtime distributables into .install/.')
    parser.add_argument('--build-dir', default='builddir', help='Meson build directory')
    parser.add_argument('--assets-dir', default='assets', help='Repository assets directory')
    parser.add_argument('--install-dir', default='.install', help='Output staging directory')
    parser.add_argument('--base-game', default='basew', help='Base game directory name')
    args = parser.parse_args()

    build_dir = pathlib.Path(args.build_dir).resolve()
    install_dir = pathlib.Path(args.install_dir).resolve()
    repo_root = pathlib.Path(__file__).resolve().parent.parent

    if not build_dir.is_dir():
        raise SystemExit(f'Build directory not found: {build_dir}')

    install_dir.mkdir(parents=True, exist_ok=True)

    copied_runtime = copy_runtime_files(build_dir, install_dir)
    if copied_runtime == 0:
        raise SystemExit(f'No runtime binaries found in {build_dir}')
    copied_runtime_dependencies = copy_runtime_dependencies(build_dir, install_dir)

    copy_base_game_tree(build_dir, install_dir, args.base_game)
    copied_reference_maps = stage_q2aas_reference_maps(repo_root, install_dir, args.base_game)

    print(f'Wrote staged runtime to {install_dir}')
    print(f'Copied {copied_runtime} root runtime files')
    if copied_runtime_dependencies:
        print(f'Copied {copied_runtime_dependencies} root runtime dependenc{"y" if copied_runtime_dependencies == 1 else "ies"}')
    print(f'Copied {args.base_game} runtime tree from {build_dir / args.base_game}')
    if copied_reference_maps:
        print(f'Copied {copied_reference_maps} q2aas reference map(s)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
