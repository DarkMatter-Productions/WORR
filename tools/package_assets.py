#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil
import zipfile


BOTFILE_SUPPORT_MEMBERS = (
    'botfiles/bots.txt',
    'botfiles/chars.h',
    'botfiles/fw_items.c',
    'botfiles/fw_weap.c',
    'botfiles/inv.h',
    'botfiles/teamplay.h',
)
BOTFILE_PROFILE_SUFFIXES = ('_c.c', '_i.c', '_t.c', '_w.c')
BOTFILE_CHARACTER_SUFFIX = '_c.c'
BOTFILE_SCRIPT_SUFFIX = '_s.c'
DEFAULT_LOOSE_ASSET_PATHS = (
    'botfiles',
    'ui/rml',
    'renderer_parity',
    'maps/worr_fr01_bmodel_first_frame.bsp',
    'maps/worr_fr01_bmodel_instances.bsp',
    'maps/worr_fr01_bmodel_cull.bsp',
    'maps/worr_fr01_lightgrid.bsp',
    'maps/worr_fr01_lightgrid_no_receiver.bsp',
    'maps/worr_fr01_lightgrid_inline_receiver.bsp',
    'maps/worr_fr01_bmodel_instances_fog.bsp',
    'maps/worr_fr01_bmodel_instances_lightmapped.bsp',
    'maps/worr_fr01_bmodel_instances_lightmapped_fog.bsp',
    'maps/worr_fr01_world_texture_replace.bsp',
    'maps/worr_fr01_world_texture_replace_fog.bsp',
    'maps/worr_fr01_alpha_test.bsp',
    'maps/worr_fr02_fence_cutout.bsp',
    'maps/worr_fr02_sprite_blend.bsp',
    'maps/worr_fr02_sprite_replacement.bsp',
    'maps/worr_fr02_sprite_depth_write.bsp',
    'maps/worr_fr01_beam_fog.bsp',
    'maps/worr_fr01_global_fog.bsp',
    'maps/worr_fr01_flare_fog.bsp',
    'maps/worr_fr01_glowmap.bsp',
    'maps/worr_fr01_lightmap_saturation.bsp',
    'maps/worr_fr01_model_glowmap.bsp',
    'maps/worr_fr01_model_md5_multijoint.bsp',
    'maps/worr_fr01_model_shell.bsp',
    'maps/worr_fr01_model_rim.bsp',
    'maps/worr_fr01_model_rim_occluded.bsp',
    'maps/worr_fr01_viewweapon_shell_bloom.bsp',
    'maps/worr_fr01_viewweapon_shell_bloom_refraction.bsp',
    'maps/worr_fr01_underwater_waterwarp.bsp',
    'maps/worr_fr01_height_fog.bsp',
    'maps/worr_fr01_sprite_fog.bsp',
    'maps/worr_fr01_liquid_entity_ordering.bsp',
    'maps/worr_fr01_transparent_ordering.bsp',
    'maps/worr_fr01_transparent_fog.bsp',
    'maps/worr_fr01_warp_flow.bsp',
    'maps/worr_fr10_rewind_mover.bsp',
    'sprites/worr_fr02_truecolor_alpha.sp2',
    'sprites/worr_fr02_sprite_replacement.sp2',
    'sprites/worr_fr02_sprite_opaque.sp2',
    'sprites/worr_fr02_sprite_alpha.sp2',
    'textures/parity',
)
RMLUI_LOOSE_ASSET_ROOT = 'ui/rml'
Q2AAS_TOOL_BINARY_STEMS = ('worr_q2aas', 'q2aas', 'bspc')
Q2AAS_TOOL_BINARY_EXTENSIONS = ('', '.exe', '.pdb', '.dll', '.so', '.dylib')


def collect_files(root: pathlib.Path) -> list[pathlib.Path]:
    return sorted(path for path in root.rglob('*') if path.is_file())


def is_q2aas_tool_binary(path: pathlib.Path) -> bool:
    suffix = path.suffix.lower()
    if suffix not in Q2AAS_TOOL_BINARY_EXTENSIONS:
        return False

    stem = path.stem.lower()
    return any(stem == value or stem.startswith(f'{value}_') for value in Q2AAS_TOOL_BINARY_STEMS)


def q2aas_tool_binary_members(paths: list[pathlib.Path], root: pathlib.Path) -> list[str]:
    members: list[str] = []
    for path in paths:
        if is_q2aas_tool_binary(path):
            members.append(path.relative_to(root).as_posix())
    return sorted(members)


def validate_no_q2aas_tool_binaries(paths: list[pathlib.Path], root: pathlib.Path) -> None:
    members = q2aas_tool_binary_members(paths, root)
    if not members:
        return

    details = '\n  - '.join(members)
    raise SystemExit(
        'q2aas/BSPC tool binaries are not packaged by default. '
        'Remove these files from the asset payload before packaging:\n'
        f'  - {details}'
    )


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def remove_path(path: pathlib.Path) -> None:
    if not path.exists() and not path.is_symlink():
        return
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    else:
        path.unlink()


def validated_relative_path(value: str) -> pathlib.Path:
    rel = pathlib.PurePosixPath(value.replace('\\', '/'))
    if rel.is_absolute() or not rel.parts or any(part in ('', '.', '..') for part in rel.parts):
        raise SystemExit(f'Invalid loose asset path: {value!r}')
    return pathlib.Path(*rel.parts)


def path_from_member(member: str) -> pathlib.Path:
    rel = pathlib.PurePosixPath(member)
    return pathlib.Path(*rel.parts)


def bot_name_from_suffix(path: pathlib.Path, suffix: str) -> str | None:
    name = path.name
    if not name.endswith(suffix):
        return None
    bot_name = name[:-len(suffix)]
    return bot_name or None


def collect_bot_names(directory: pathlib.Path, suffix: str) -> set[str]:
    if not directory.is_dir():
        return set()

    names: set[str] = set()
    for path in directory.glob(f'*{suffix}'):
        if not path.is_file():
            continue
        bot_name = bot_name_from_suffix(path, suffix)
        if bot_name is not None:
            names.add(bot_name)
    return names


def botfile_release_members(assets_dir: pathlib.Path) -> list[str]:
    botfiles_dir = assets_dir / 'botfiles'
    bots_dir = botfiles_dir / 'bots'
    scripts_dir = botfiles_dir / 'scripts'
    failures: list[str] = []

    if not botfiles_dir.is_dir():
        raise SystemExit(f'Required botfiles directory not found: {botfiles_dir}')
    if not bots_dir.is_dir():
        failures.append(f'missing bot profile directory: {bots_dir}')
    if not scripts_dir.is_dir():
        failures.append(f'missing bot script directory: {scripts_dir}')

    for member in BOTFILE_SUPPORT_MEMBERS:
        source = assets_dir / path_from_member(member)
        if not source.is_file():
            failures.append(f'missing botfile support asset: {member}')

    character_bots = collect_bot_names(bots_dir, BOTFILE_CHARACTER_SUFFIX)
    script_bots = collect_bot_names(scripts_dir, BOTFILE_SCRIPT_SUFFIX)
    if not character_bots:
        failures.append(f'no bot character profiles found in {bots_dir}')
    if not script_bots:
        failures.append(f'no bot scripts found in {scripts_dir}')

    for bot_name in sorted(character_bots):
        for suffix in BOTFILE_PROFILE_SUFFIXES:
            member = f'botfiles/bots/{bot_name}{suffix}'
            if not (assets_dir / path_from_member(member)).is_file():
                failures.append(f'missing bot profile companion: {member}')

        script_member = f'botfiles/scripts/{bot_name}{BOTFILE_SCRIPT_SUFFIX}'
        if not (assets_dir / path_from_member(script_member)).is_file():
            failures.append(f'missing bot script companion: {script_member}')

    for bot_name in sorted(script_bots - character_bots):
        failures.append(
            f'bot script has no character profile: '
            f'botfiles/scripts/{bot_name}{BOTFILE_SCRIPT_SUFFIX}'
        )

    for suffix in BOTFILE_PROFILE_SUFFIXES[1:]:
        companion_bots = collect_bot_names(bots_dir, suffix)
        for bot_name in sorted(companion_bots - character_bots):
            failures.append(
                f'bot profile companion has no character profile: '
                f'botfiles/bots/{bot_name}{suffix}'
            )

    members = sorted(
        path.relative_to(assets_dir).as_posix()
        for path in botfiles_dir.rglob('*')
        if path.is_file()
    )
    if not members:
        failures.append(f'no botfile assets found in {botfiles_dir}')

    if failures:
        details = '\n  - '.join(failures)
        raise SystemExit(f'Invalid botfile release payload:\n  - {details}')
    return members


def botfile_archive_member_requirements(assets_dir: pathlib.Path) -> list[str]:
    return [
        f'{member}={sha256_file(assets_dir / path_from_member(member))}'
        for member in botfile_release_members(assets_dir)
    ]


def rmlui_release_members(assets_dir: pathlib.Path) -> list[str]:
    rel = path_from_member(RMLUI_LOOSE_ASSET_ROOT)
    source = assets_dir / rel
    if not source.exists():
        return []
    if not source.is_dir():
        raise SystemExit(f'RmlUi loose asset root must be a directory: {source}')

    members = sorted(
        path.relative_to(assets_dir).as_posix()
        for path in source.rglob('*')
        if path.is_file()
    )
    if not members:
        raise SystemExit(f'RmlUi loose asset root contains no files: {source}')
    return members


def archive_member_hashes(archive_path: pathlib.Path) -> dict[str, str]:
    try:
        with zipfile.ZipFile(archive_path) as archive:
            members: dict[str, str] = {}
            for info in archive.infolist():
                member = info.filename.replace('\\', '/')
                if member.endswith('/'):
                    continue
                if member in members:
                    raise SystemExit(f'Duplicate archive member in {archive_path}: {member}')
                members[member] = hashlib.sha256(archive.read(info)).hexdigest()
            return members
    except zipfile.BadZipFile as exc:
        raise SystemExit(f'Package archive is not a readable zip/pkz file: {archive_path}: {exc}') from exc
    except OSError as exc:
        raise SystemExit(f'Unable to read package archive {archive_path}: {exc}') from exc


def validate_botfile_payload(
    assets_dir: pathlib.Path,
    output_dir: pathlib.Path,
    archive_path: pathlib.Path,
    members: list[str],
) -> None:
    archive_hashes = archive_member_hashes(archive_path)
    failures: list[str] = []

    for member in members:
        source = assets_dir / path_from_member(member)
        expected_hash = sha256_file(source)

        archive_hash = archive_hashes.get(member)
        if archive_hash is None:
            failures.append(f'missing archive member: {member}')
        elif archive_hash.lower() != expected_hash.lower():
            failures.append(
                f'archive member hash mismatch for {member}: '
                f'expected {expected_hash}, got {archive_hash}'
            )

        loose_path = output_dir / path_from_member(member)
        if not loose_path.is_file():
            failures.append(f'missing loose botfile mirror: {loose_path}')
            continue

        loose_hash = sha256_file(loose_path)
        if loose_hash.lower() != expected_hash.lower():
            failures.append(
                f'loose botfile hash mismatch for {loose_path}: '
                f'expected {expected_hash}, got {loose_hash}'
            )

    if failures:
        details = '\n  - '.join(failures)
        raise SystemExit(f'Invalid packaged botfile payload:\n  - {details}')


def validate_rmlui_payload(
    assets_dir: pathlib.Path,
    output_dir: pathlib.Path,
    archive_path: pathlib.Path,
    members: list[str],
) -> None:
    archive_hashes = archive_member_hashes(archive_path)
    failures: list[str] = []

    for member in members:
        source = assets_dir / path_from_member(member)
        expected_hash = sha256_file(source)

        archive_hash = archive_hashes.get(member)
        if archive_hash is None:
            failures.append(f'missing archive member: {member}')
        elif archive_hash.lower() != expected_hash.lower():
            failures.append(
                f'archive member hash mismatch for {member}: '
                f'expected {expected_hash}, got {archive_hash}'
            )

        loose_path = output_dir / path_from_member(member)
        if not loose_path.is_file():
            failures.append(f'missing loose RmlUi asset mirror: {loose_path}')
            continue

        loose_hash = sha256_file(loose_path)
        if loose_hash.lower() != expected_hash.lower():
            failures.append(
                f'loose RmlUi asset hash mismatch for {loose_path}: '
                f'expected {expected_hash}, got {loose_hash}'
            )

    if failures:
        details = '\n  - '.join(failures)
        raise SystemExit(f'Invalid packaged RmlUi asset payload:\n  - {details}')


def mirror_loose_asset(assets_dir: pathlib.Path, output_dir: pathlib.Path, rel: pathlib.Path) -> bool:
    source = assets_dir / rel
    dest = output_dir / rel

    remove_path(dest)
    if not source.exists():
        return False

    dest.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, dest)
        return True
    if source.is_file():
        shutil.copy2(source, dest)
        return True
    raise SystemExit(f'Unsupported loose asset source: {source}')


def main() -> int:
    parser = argparse.ArgumentParser(description='Package WORR runtime assets into a staged game archive.')
    parser.add_argument('--assets-dir', default='assets', help='Source assets directory')
    parser.add_argument('--install-dir', default='.install', help='Install staging directory')
    parser.add_argument('--base-game', default='basew', help='Output game directory name inside the install root')
    parser.add_argument('--archive-name', default='pak0.pkz', help='Output archive filename')
    parser.add_argument(
        '--output-path',
        help='Optional output archive path relative to <install-dir>; overrides --base-game/--archive-name',
    )
    parser.add_argument(
        '--loose-dir',
        action='append',
        default=list(DEFAULT_LOOSE_ASSET_PATHS),
        help='Asset directory/file to mirror loose beside the archive; repeatable, relative to --assets-dir',
    )
    args = parser.parse_args()

    assets_dir = pathlib.Path(args.assets_dir).resolve()
    install_dir = pathlib.Path(args.install_dir).resolve()
    if args.output_path:
        archive_path = install_dir / pathlib.Path(args.output_path)
        output_dir = archive_path.parent
    else:
        output_dir = install_dir / args.base_game
        archive_path = output_dir / args.archive_name

    if not assets_dir.is_dir():
        raise SystemExit(f'Assets directory not found: {assets_dir}')

    files = collect_files(assets_dir)
    if not files:
        raise SystemExit(f'No files found in assets directory: {assets_dir}')
    validate_no_q2aas_tool_binaries(files, assets_dir)
    botfile_members = botfile_release_members(assets_dir)
    rmlui_members = rmlui_release_members(assets_dir)

    output_dir.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, path.relative_to(assets_dir).as_posix())

    mirrored: list[str] = []
    for value in args.loose_dir:
        rel = validated_relative_path(value)
        if mirror_loose_asset(assets_dir, output_dir, rel):
            mirrored.append(rel.as_posix())

    validate_botfile_payload(assets_dir, output_dir, archive_path, botfile_members)
    if rmlui_members:
        validate_rmlui_payload(assets_dir, output_dir, archive_path, rmlui_members)

    print(f'Wrote {archive_path}')
    print(f'Packed {len(files)} files from {assets_dir}')
    print(f'Validated botfile release payload: {len(botfile_members)} package/loose file(s)')
    if rmlui_members:
        print(f'Validated RmlUi asset payload: {len(rmlui_members)} package/loose file(s)')
    if mirrored:
        print(f'Mirrored loose asset paths: {", ".join(mirrored)}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
