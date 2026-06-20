#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
from typing import Any

TOOLS_DIR = pathlib.Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from package_assets import botfile_archive_member_requirements, q2aas_tool_binary_members


RELEASE_NOTICE_FILES = (
    ("LICENSE", "licenses/WORR-LICENSE.txt"),
    ("tools/q2aas/LICENSE", "licenses/q2aas-bspc-LICENSE.txt"),
    ("docs-dev/q3a-botlib-aas-credits.md", "licenses/q3a-botlib-aas-credits.md"),
    ("tools/q2aas/README.WORR.md", "licenses/q2aas-README.WORR.md"),
    ("src/game/sgame/bots/q3a/README.WORR.md", "licenses/q3a-botlib-README.WORR.md"),
)


def run_step(label: str, command: list[str]) -> None:
    print(f"[refresh-install] {label}", flush=True)
    subprocess.run(command, check=True)


def rel_path(value: str) -> pathlib.Path:
    return pathlib.Path(*pathlib.PurePosixPath(value).parts)


def release_notice_destinations() -> list[str]:
    return [dest for _, dest in RELEASE_NOTICE_FILES]


def validate_q2aas_tool_binary_policy(install_dir: pathlib.Path) -> None:
    files = sorted(path for path in install_dir.rglob("*") if path.is_file())
    members = q2aas_tool_binary_members(files, install_dir)
    if not members:
        return

    details = "\n  - ".join(members)
    raise SystemExit(
        "q2aas/BSPC tool binaries are not part of default WORR binary releases. "
        "Remove these staged files or add a dedicated opt-in release flow with "
        "license/credit sidecar validation:\n"
        f"  - {details}"
    )


def validate_release_notice_bundle(install_dir: pathlib.Path) -> None:
    failures: list[str] = []
    for dest in release_notice_destinations():
        path = install_dir / rel_path(dest)
        if not path.is_file():
            failures.append(f"missing release notice sidecar: {dest}")
        elif path.stat().st_size == 0:
            failures.append(f"empty release notice sidecar: {dest}")

    if failures:
        details = "\n  - ".join(failures)
        raise SystemExit(f"Invalid release notice bundle:\n  - {details}")


def stage_release_notice_bundle(repo_root: pathlib.Path, install_dir: pathlib.Path) -> None:
    notices_dir = install_dir / "licenses"
    if notices_dir.exists():
        shutil.rmtree(notices_dir)

    for source_rel, dest_rel in RELEASE_NOTICE_FILES:
        source = repo_root / rel_path(source_rel)
        if not source.is_file():
            raise SystemExit(f"Required release notice source not found: {source}")
        dest = install_dir / rel_path(dest_rel)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)

    validate_release_notice_bundle(install_dir)


def is_under(path: pathlib.Path, root: pathlib.Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def load_q2aas_stage_report(path: pathlib.Path) -> dict[str, Any]:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise SystemExit(f"Unable to read q2aas stage report {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"Invalid q2aas stage report JSON {path}: {exc}") from exc

    if not isinstance(report, dict):
        raise SystemExit(f"Invalid q2aas stage report root: expected object in {path}")
    if not isinstance(report.get("maps"), list):
        raise SystemExit(f"Invalid q2aas stage report: expected maps array in {path}")
    return report


def archive_member_for(staged_aas: pathlib.Path, base_game_dir: pathlib.Path) -> str:
    if is_under(staged_aas, base_game_dir):
        return staged_aas.relative_to(base_game_dir).as_posix()
    return f"maps/{staged_aas.name}"


def normalize_sha256(value: Any, label: str, report_path: pathlib.Path) -> str:
    if not isinstance(value, str) or not value.strip():
        raise SystemExit(f"{label}: staged AAS SHA-256 is missing in {report_path}")

    normalized = value.strip().lower()
    if len(normalized) != 64 or any(character not in "0123456789abcdef" for character in normalized):
        raise SystemExit(f"{label}: invalid staged AAS SHA-256 in {report_path}: {value!r}")
    return normalized


def q2aas_archive_member_requirements(
    stage_report_path: pathlib.Path,
    install_dir: pathlib.Path,
    base_game: str,
) -> list[str]:
    report = load_q2aas_stage_report(stage_report_path)
    base_game_dir = (install_dir / base_game).resolve()
    requirements_by_member: dict[str, str] = {}

    for index, map_entry in enumerate(report.get("maps", [])):
        if not isinstance(map_entry, dict):
            continue
        staged_output = map_entry.get("staged_output")
        if not isinstance(staged_output, dict) or not staged_output.get("enabled"):
            continue

        map_id = map_entry.get("id")
        if not isinstance(map_id, str) or not map_id:
            map_id = f"maps[{index}]"

        staged_value = staged_output.get("aas")
        if not isinstance(staged_value, str) or not staged_value:
            raise SystemExit(f"{map_id}: staged_output.aas is missing in {stage_report_path}")

        staged_aas = pathlib.Path(staged_value)
        if not staged_aas.is_absolute():
            staged_aas = base_game_dir / staged_aas
        staged_aas = staged_aas.resolve()

        if staged_aas.suffix.lower() != ".aas":
            raise SystemExit(f"{map_id}: staged output is not an .aas file: {staged_aas}")

        member = archive_member_for(staged_aas, base_game_dir)
        expected_hash = staged_output.get("aas_sha256") or map_entry.get("aas_sha256")
        normalized_hash = normalize_sha256(expected_hash, map_id, stage_report_path)
        previous_hash = requirements_by_member.get(member)
        if previous_hash is not None and previous_hash != normalized_hash:
            raise SystemExit(
                f"{map_id}: conflicting staged AAS hashes for archive member {member} in {stage_report_path}"
            )
        requirements_by_member[member] = normalized_hash

    return [f"{member}={expected_hash}" for member, expected_hash in requirements_by_member.items()]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Refresh the local .install staging root with current WORR binaries and assets."
    )
    parser.add_argument("--build-dir", default="builddir", help="Meson build directory")
    parser.add_argument("--assets-dir", default="assets", help="Repository assets directory")
    parser.add_argument("--install-dir", default=".install", help="Install staging directory")
    parser.add_argument("--base-game", default="basew", help="Base game directory name")
    parser.add_argument(
        "--archive-name",
        default="pak0.pkz",
        help="Generated asset archive name inside <install>/<base-game>",
    )
    parser.add_argument(
        "--platform-id",
        default="",
        help="Optional release platform id for post-stage validation (for example windows-x86_64)",
    )
    parser.add_argument(
        "--package-q2aas-aas",
        action="store_true",
        help="After packaging assets, inject validated q2aas staged AAS files into the base game archive.",
    )
    parser.add_argument(
        "--q2aas-stage-report",
        default=".tmp/q2aas/stage-report.json",
        help="q2aas stage report consumed by --package-q2aas-aas.",
    )
    parser.add_argument(
        "--q2aas-package-report",
        default=".tmp/q2aas/package-archive-report.json",
        help="q2aas package archive report written by --package-q2aas-aas.",
    )
    parser.add_argument(
        "--q2aas-package-audit-report",
        default=".tmp/q2aas/package-archive-audit-report.json",
        help="q2aas archive-required package audit report written by --package-q2aas-aas.",
    )
    args = parser.parse_args()

    repo_root = pathlib.Path(__file__).resolve().parent.parent
    tools_dir = repo_root / "tools"
    python_exe = pathlib.Path(sys.executable).resolve()

    stage_script = tools_dir / "stage_install.py"
    assets_script = tools_dir / "package_assets.py"
    validate_script = tools_dir / "release" / "validate_stage.py"
    q2aas_package_script = tools_dir / "q2aas" / "package_worr_q2aas_archive.py"
    q2aas_audit_script = tools_dir / "q2aas" / "audit_worr_q2aas_package.py"

    run_step(
        "Stage runtime and base game tree",
        [
            str(python_exe),
            str(stage_script),
            "--build-dir",
            args.build_dir,
            "--assets-dir",
            args.assets_dir,
            "--install-dir",
            args.install_dir,
            "--base-game",
            args.base_game,
        ],
    )

    install_dir = pathlib.Path(args.install_dir).resolve()
    validate_q2aas_tool_binary_policy(install_dir)
    stage_release_notice_bundle(repo_root, install_dir)

    run_step(
        f"Package staged runtime assets ({args.base_game}/{args.archive_name})",
        [
            str(python_exe),
            str(assets_script),
            "--assets-dir",
            args.assets_dir,
            "--install-dir",
            args.install_dir,
            "--base-game",
            args.base_game,
            "--archive-name",
            args.archive_name,
        ],
    )

    if args.package_q2aas_aas:
        run_step(
            f"Package validated q2aas AAS into {args.base_game}/{args.archive_name}",
            [
                str(python_exe),
                str(q2aas_package_script),
                "--report-json",
                args.q2aas_stage_report,
                "--install-dir",
                args.install_dir,
                "--base-game",
                args.base_game,
                "--archive-name",
                args.archive_name,
                "--package-report-json",
                args.q2aas_package_report,
            ],
        )

        run_step(
            "Audit q2aas packaged AAS archive members",
            [
                str(python_exe),
                str(q2aas_audit_script),
                "--report-json",
                args.q2aas_stage_report,
                "--install-dir",
                args.install_dir,
                "--base-game",
                args.base_game,
                "--archive-name",
                args.archive_name,
                "--require-archive-member",
                "--audit-report-json",
                args.q2aas_package_audit_report,
            ],
        )

    if args.platform_id:
        validation_command = [
            str(python_exe),
            str(validate_script),
            "--install-dir",
            args.install_dir,
            "--base-game",
            args.base_game,
            "--archive-name",
            args.archive_name,
            "--platform-id",
            args.platform_id,
        ]
        for requirement in botfile_archive_member_requirements(pathlib.Path(args.assets_dir).resolve()):
            validation_command.extend(["--required-archive-member", requirement])
        if args.package_q2aas_aas:
            for requirement in q2aas_archive_member_requirements(
                pathlib.Path(args.q2aas_stage_report).resolve(),
                pathlib.Path(args.install_dir).resolve(),
                args.base_game,
            ):
                validation_command.extend(["--required-archive-member", requirement])

        run_step(
            f"Validate staged payload ({args.platform_id})",
            validation_command,
        )

    print(f"[refresh-install] Completed: {install_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
