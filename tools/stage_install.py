#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import shutil


RUNTIME_EXTENSIONS = {"", ".exe", ".dll", ".pdb", ".so", ".dylib"}
RUNTIME_PATTERNS = ('worr_*', 'worr*.exe', 'worr*.dll', 'worr*.pdb', 'worr*.so', 'worr*.dylib')
GENERATED_BASE_GAME_PATTERNS = ('cgame_*', 'sgame_*', 'shader_vkpt', 'pak0.pkz')


def is_runtime_file(path: pathlib.Path) -> bool:
    return path.is_file() and path.suffix.lower() in RUNTIME_EXTENSIONS


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


def copy_runtime_files(build_dir: pathlib.Path, install_dir: pathlib.Path) -> int:
    copied = 0
    copied_paths: set[pathlib.Path] = set()

    clear_globbed_paths(install_dir, RUNTIME_PATTERNS)
    for pattern in RUNTIME_PATTERNS:
        for path in sorted(build_dir.glob(pattern)):
            if not is_runtime_file(path) or path in copied_paths:
                continue
            shutil.copy2(path, install_dir / path.name)
            copied_paths.add(path)
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


def main() -> int:
    parser = argparse.ArgumentParser(description='Stage WORR runtime distributables into .install/.')
    parser.add_argument('--build-dir', default='builddir', help='Meson build directory')
    parser.add_argument('--assets-dir', default='assets', help='Repository assets directory')
    parser.add_argument('--install-dir', default='.install', help='Output staging directory')
    parser.add_argument('--base-game', default='basew', help='Base game directory name')
    args = parser.parse_args()

    build_dir = pathlib.Path(args.build_dir).resolve()
    install_dir = pathlib.Path(args.install_dir).resolve()

    if not build_dir.is_dir():
        raise SystemExit(f'Build directory not found: {build_dir}')

    install_dir.mkdir(parents=True, exist_ok=True)

    copied_runtime = copy_runtime_files(build_dir, install_dir)
    if copied_runtime == 0:
        raise SystemExit(f'No runtime binaries found in {build_dir}')

    copy_base_game_tree(build_dir, install_dir, args.base_game)

    print(f'Wrote staged runtime to {install_dir}')
    print(f'Copied {copied_runtime} root runtime files')
    print(f'Copied {args.base_game} runtime tree from {build_dir / args.base_game}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
