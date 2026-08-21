#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil
import sys
import tempfile


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.atomic_directory import publish_staged_directory_atomically


RUNTIME_EXTENSIONS = {"", ".exe", ".dll", ".pdb", ".so", ".dylib"}
RUNTIME_PATTERNS = ('worr_*', 'worr*.exe', 'worr*.dll', 'worr*.pdb', 'worr*.so', 'worr*.dylib')
LEGACY_RUNTIME_PATTERNS = ('worr_runtime_*', 'worr_ded_runtime_*')
RUNTIME_DEPENDENCIES = ('rmlui_core.dll',)
GENERATED_BASE_GAME_PATTERNS = ('cgame*', 'sgame*', 'shader_vkpt', 'pak0.pkz')
RUNTIME_USER_DATA_PATTERNS = (
    '*.cfg',
    '.conhistory',
    'save',
    'saves',
    'screenshots',
    'demos',
    'logs',
)
GAME_MODULE_PREFIXES = ('cgame_', 'sgame_')
GAME_MODULE_BINARY_SUFFIXES = {'.dll', '.so', '.dylib'}
Q2AAS_REFERENCE_MAP_DIR = pathlib.Path("tools") / "q2aas" / "reference_maps"
RUNTIME_COORDINATION_LOCK = "worr_install.lock"


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


def clear_globbed_paths(
    root: pathlib.Path,
    patterns: tuple[str, ...],
    *,
    preserved_paths: frozenset[pathlib.Path] = frozenset(),
) -> None:
    for pattern in patterns:
        for path in sorted(root.glob(pattern)):
            if path in preserved_paths:
                continue
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


def copy_verified_file(source: pathlib.Path, dest: pathlib.Path, description: str) -> bool:
    try:
        remove_path(dest)
    except PermissionError:
        # A scanner or debugger can transiently deny deletion of a loaded image
        # even when no WORR process owns it. Retaining that file is safe only
        # when it is already byte-for-byte the requested build artifact.
        if not dest.is_file() or file_sha256(source) != file_sha256(dest):
            raise
        return False

    shutil.copy2(source, dest)
    if file_sha256(source) != file_sha256(dest):
        raise SystemExit(f'{description} staging verification failed for {source} -> {dest}')
    return True


def copy_runtime_files(build_dir: pathlib.Path, install_dir: pathlib.Path) -> int:
    copied = 0
    copied_paths: set[pathlib.Path] = set()
    runtime_sources: list[pathlib.Path] = []

    for pattern in RUNTIME_PATTERNS:
        for path in sorted(build_dir.glob(pattern)):
            if not should_stage_runtime_file(path) or path in copied_paths:
                continue
            copied_paths.add(path)
            runtime_sources.append(path)

    desired_destinations = frozenset(install_dir / path.name for path in runtime_sources)
    clear_globbed_paths(
        install_dir,
        RUNTIME_PATTERNS + LEGACY_RUNTIME_PATTERNS,
        preserved_paths=desired_destinations,
    )
    remove_path(install_dir / "bin")
    for path in runtime_sources:
        dest = install_dir / path.name
        copy_verified_file(path, dest, "Runtime")
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

    # The Meson base-game tree is also a valid local runtime directory and can
    # therefore contain archived cvars, console history, saves, screenshots,
    # demos, or logs from a developer launch. Those files are never build
    # products and must not cross into a distributable staging tree.
    clear_globbed_paths(target, RUNTIME_USER_DATA_PATTERNS)

    # Meson's build root also contains cgame_*/sgame_* test executables. Remove
    # every generated module-shaped file copied from the install tree, then
    # repopulate only the actual shared modules (and their optional PDBs) from
    # the authoritative build outputs.
    clear_globbed_paths(target, ('cgame*', 'sgame*'))

    # Meson links game modules in the build root; the generated base-game tree can
    # lag behind when a single module is rebuilt. Overlay the root artifacts last.
    for prefix in GAME_MODULE_PREFIXES:
        module_binaries = [
            path
            for path in sorted(build_dir.glob(f'{prefix}*'))
            if path.is_file() and path.suffix.lower() in GAME_MODULE_BINARY_SUFFIXES
        ]
        if len(module_binaries) != 1:
            raise SystemExit(
                f'Expected exactly one {prefix.rstrip("_")} shared module in '
                f'{build_dir}, found {len(module_binaries)}'
            )

        module_binary = module_binaries[0]
        module_sources = [module_binary]
        debug_symbols = module_binary.with_suffix('.pdb')
        if debug_symbols.is_file():
            module_sources.append(debug_symbols)

        for path in module_sources:
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


def create_runtime_coordination_lock(install_dir: pathlib.Path) -> pathlib.Path:
    """Provision the stable, never-replaced updater/runtime lock file."""
    lock_path = install_dir / RUNTIME_COORDINATION_LOCK
    lock_path.write_bytes(b"")
    lock_path.chmod(0o644)
    return lock_path


def main() -> int:
    parser = argparse.ArgumentParser(description='Stage WORR runtime distributables into .install/.')
    parser.add_argument('--build-dir', default='builddir', help='Meson build directory')
    parser.add_argument('--assets-dir', default='assets', help='Repository assets directory')
    parser.add_argument('--install-dir', default='.install', help='Output staging directory')
    parser.add_argument('--base-game', default='basew', help='Base game directory name')
    args = parser.parse_args()

    build_dir = pathlib.Path(args.build_dir).resolve()
    requested_install_dir = pathlib.Path(args.install_dir)
    if requested_install_dir.is_symlink():
        raise SystemExit(
            f"Refusing to replace symbolic-link install directory: {requested_install_dir}"
        )
    install_dir = requested_install_dir.resolve()
    repo_root = REPO_ROOT

    if not build_dir.is_dir():
        raise SystemExit(f'Build directory not found: {build_dir}')
    if build_dir == install_dir or build_dir in install_dir.parents or install_dir in build_dir.parents:
        raise SystemExit(
            f'Build and install directories must not overlap: {build_dir} -> {install_dir}'
        )

    install_dir.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f'.{install_dir.name}.stage.',
        dir=install_dir.parent,
    ) as temp_dir:
        staged_install = pathlib.Path(temp_dir) / 'install'
        staged_install.mkdir()

        copied_runtime = copy_runtime_files(build_dir, staged_install)
        if copied_runtime == 0:
            raise SystemExit(f'No runtime binaries found in {build_dir}')
        copied_runtime_dependencies = copy_runtime_dependencies(
            build_dir,
            staged_install,
        )
        create_runtime_coordination_lock(staged_install)

        copy_base_game_tree(build_dir, staged_install, args.base_game)
        copied_reference_maps = stage_q2aas_reference_maps(
            repo_root,
            staged_install,
            args.base_game,
        )
        publish_staged_directory_atomically(staged_install, install_dir)

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
