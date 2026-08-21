#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import sys
import tempfile
from types import SimpleNamespace

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.package_release import (
    DEFAULT_PRESERVE,
    MAX_EXPANDED_UPDATE_BYTES,
    MAX_LOCAL_MANIFEST_BYTES,
    build_files_payload,
    build_local_install_manifest,
    build_update_config,
    resolve_release_trust_policy,
    should_include,
    validate_non_overlapping_directories,
    validate_updater_install_plan,
    validated_archive_filename,
    validated_archive_path,
)
from tools.atomic_directory import publish_staged_directory_atomically
from tools.release.stage_release_layout import stage_release_layout
from tools.release.targets import get_target


def copy_filtered_tree(
    source_root: pathlib.Path,
    dest_root: pathlib.Path,
    *,
    include: list[str],
    exclude: list[str],
) -> None:
    if dest_root.exists():
        shutil.rmtree(dest_root)
    dest_root.mkdir(parents=True, exist_ok=True)

    for source in sorted(path for path in source_root.rglob("*") if path.is_file()):
        rel_path = source.relative_to(source_root).as_posix()
        if not should_include(rel_path, include, exclude):
            continue
        dest = dest_root / rel_path
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Prepare a role-specific shipped install tree with updater metadata."
    )
    parser.add_argument("--input-dir", required=True, help="Combined staged install directory (.install)")
    parser.add_argument("--output-dir", required=True, help="Role-specific output install directory")
    parser.add_argument("--platform-id", required=True, help="Release platform id")
    parser.add_argument("--role", required=True, choices=["client", "server"], help="Install role to stage")
    parser.add_argument("--repo", required=True, help="GitHub owner/repo")
    parser.add_argument("--channel", required=True, help="Release channel")
    parser.add_argument("--version", required=True, help="Release version")
    parser.add_argument("--commit-sha", default="", help="Source commit sha")
    parser.add_argument("--build-id", default="", help="Build id for traceability")
    parser.add_argument("--allow-prerelease", action="store_true", help="Enable prerelease updater channel")
    parser.add_argument(
        "--release-trust-policy",
        help="Schema-1 public Ed25519 trust policy embedded in the shipped updater config",
    )
    parser.add_argument(
        "--allow-unsigned-development-config",
        action="store_true",
        help="Allow an unsigned updater config only for the explicit development channel",
    )
    args = parser.parse_args()

    release_trust_policy_document = resolve_release_trust_policy(
        args.release_trust_policy,
        allow_unsigned_development_config=args.allow_unsigned_development_config,
        repo=args.repo,
        channel=args.channel,
    )

    target = get_target(args.platform_id)
    config = target[args.role]
    input_dir = pathlib.Path(args.input_dir).resolve()
    requested_output_dir = pathlib.Path(args.output_dir)
    if requested_output_dir.is_symlink():
        raise SystemExit(
            f"Refusing to replace symbolic-link output directory: {requested_output_dir}"
        )
    output_dir = requested_output_dir.resolve()
    if not input_dir.is_dir():
        raise SystemExit(f"Input directory not found: {input_dir}")
    validate_non_overlapping_directories(input_dir, output_dir)
    output_dir.parent.mkdir(parents=True, exist_ok=True)

    local_manifest_name = validated_archive_filename(
        config["local_manifest_name"],
        "local install manifest name",
    )

    with tempfile.TemporaryDirectory(
        prefix=f".{output_dir.name}.stage-",
        dir=output_dir.parent,
    ) as temp_dir:
        release_input = pathlib.Path(temp_dir) / "release-input"
        staged_output = pathlib.Path(temp_dir) / "prepared-install"
        release_layout = target.get("release_layout", {})
        stage_release_layout(
            input_dir,
            release_input,
            relocate_root_files=release_layout.get("relocate_root_files", []),
            relocate_root_dir=release_layout.get("relocate_root_dir", "bin"),
        )

        copy_filtered_tree(
            release_input,
            staged_output,
            include=config.get("include", []),
            exclude=config.get("exclude", []),
        )

        metadata_args = SimpleNamespace(
            repo=args.repo,
            channel=args.channel,
            role=args.role,
            release_index_asset=f"worr-release-index-{args.channel}.json",
            launch_exe=config["launch_exe"],
            engine_library=config["engine_library"],
            local_manifest_name=local_manifest_name,
            autolaunch=True,
            allow_prerelease=args.allow_prerelease,
            allow_unsigned_development_config=args.allow_unsigned_development_config,
            release_trust_policy_document=release_trust_policy_document,
            preserve=list(DEFAULT_PRESERVE),
            version=args.version,
            platform_id=target["platform_id"],
            platform_os=target["os"],
            platform_arch=target["arch"],
            build_id=args.build_id,
            commit_sha=args.commit_sha,
        )

        config_path = staged_output / "worr_update.json"
        config_path.write_text(
            json.dumps(build_update_config(metadata_args), indent=2),
            encoding="utf-8",
        )

        manifest_entries: list[tuple[pathlib.Path, str]] = []
        for path in sorted(
            candidate for candidate in staged_output.rglob("*") if candidate.is_file()
        ):
            rel_path = validated_archive_path(
                path.relative_to(staged_output).as_posix(),
                "prepared install path",
            )
            if rel_path.casefold() == local_manifest_name.casefold():
                continue
            manifest_entries.append((path, rel_path))

        validate_updater_install_plan(
            [rel_path for _, rel_path in manifest_entries],
            local_manifest_name,
        )
        files = build_files_payload(manifest_entries)
        manifest = build_local_install_manifest(metadata_args, files)
        manifest_payload = json.dumps(manifest, indent=2).encode("utf-8")
        if len(manifest_payload) > MAX_LOCAL_MANIFEST_BYTES:
            raise SystemExit(
                f"Local install manifest is {len(manifest_payload)} bytes; the updater "
                f"supports at most {MAX_LOCAL_MANIFEST_BYTES} bytes"
            )
        expanded_size = sum(int(entry["size"]) for entry in files) + len(manifest_payload)
        if expanded_size > MAX_EXPANDED_UPDATE_BYTES:
            raise SystemExit(
                f"Prepared install expands to more than {MAX_EXPANDED_UPDATE_BYTES} bytes"
            )
        manifest_path = staged_output / local_manifest_name
        manifest_path.write_bytes(manifest_payload)

        publish_staged_directory_atomically(staged_output, output_dir)

    print(f"Prepared {args.role} install root: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
