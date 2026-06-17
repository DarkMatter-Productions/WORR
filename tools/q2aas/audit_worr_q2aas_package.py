#!/usr/bin/env python3
"""Audit q2aas staged AAS package readiness."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import sys
import zipfile
from typing import Any


PACKAGE_AUDIT_SCHEMA = "worr-q2aas-package-audit-v1"


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def is_under(path: pathlib.Path, root: pathlib.Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def resolve_path(root: pathlib.Path, value: str) -> pathlib.Path:
    path = pathlib.Path(value)
    if path.is_absolute():
        return path
    return root / path


def load_stage_report(path: pathlib.Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise SystemExit(f"Unable to read q2aas stage report {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"Invalid q2aas stage report JSON {path}: {exc}") from exc

    if not isinstance(payload, dict):
        raise SystemExit(f"Invalid q2aas stage report root: expected object in {path}")
    maps = payload.get("maps")
    if not isinstance(maps, list):
        raise SystemExit(f"Invalid q2aas stage report: expected maps array in {path}")
    return payload


def load_archive(path: pathlib.Path) -> tuple[dict[str, Any], dict[str, bytes], list[str]]:
    archive_report: dict[str, Any] = {
        "path": str(path),
        "exists": path.is_file(),
        "status": "missing",
    }
    members: dict[str, bytes] = {}
    issues: list[str] = []

    if not path.is_file():
        issues.append(f"missing package archive: {path}")
        return archive_report, members, issues

    archive_report.update({
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
    })

    try:
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                normalized_name = name.replace("\\", "/")
                if normalized_name.endswith("/"):
                    continue
                members[normalized_name] = archive.read(name)
    except zipfile.BadZipFile as exc:
        archive_report["status"] = "invalid"
        issues.append(f"package archive is not a readable zip/pkz file: {path}: {exc}")
        return archive_report, members, issues
    except OSError as exc:
        archive_report["status"] = "unreadable"
        issues.append(f"could not read package archive: {path}: {exc}")
        return archive_report, members, issues

    archive_report.update({
        "status": "read",
        "member_count": len(members),
    })
    return archive_report, members, issues


def staged_aas_path(staged_value: Any, base_game_dir: pathlib.Path) -> pathlib.Path | None:
    if not isinstance(staged_value, str) or not staged_value:
        return None
    staged = pathlib.Path(staged_value)
    if not staged.is_absolute():
        staged = base_game_dir / staged
    return staged.resolve()


def audit_map(
    map_entry: dict[str, Any],
    base_game_dir: pathlib.Path,
    archive_members: dict[str, bytes],
    *,
    require_archive_member: bool,
) -> dict[str, Any]:
    map_id = map_entry.get("id")
    if not isinstance(map_id, str) or not map_id:
        map_id = "<unnamed>"

    issues: list[str] = []
    staged_output = map_entry.get("staged_output")
    if not isinstance(staged_output, dict):
        staged_output = {}

    expected_hash = staged_output.get("aas_sha256") or map_entry.get("aas_sha256")
    staged = staged_aas_path(staged_output.get("aas"), base_game_dir)
    audit: dict[str, Any] = {
        "id": map_id,
        "status": "failed",
        "issues": issues,
        "expected_sha256": expected_hash,
    }

    if staged is None:
        issues.append("staged_output.aas is missing")
        return audit

    audit["loose_path"] = str(staged)
    if not is_under(staged, base_game_dir):
        issues.append(f"staged AAS is outside base game directory: {staged}")
        relative_member = pathlib.PurePosixPath("maps") / staged.name
    else:
        relative_member = pathlib.PurePosixPath(staged.relative_to(base_game_dir).as_posix())

    archive_member = relative_member.as_posix()
    audit["archive_member"] = archive_member

    loose_report: dict[str, Any] = {
        "exists": staged.is_file(),
        "status": "missing",
    }
    loose_ok = False
    if staged.is_file():
        loose_hash = sha256_file(staged)
        loose_report.update({
            "status": "present",
            "size": staged.stat().st_size,
            "sha256": loose_hash,
        })
        loose_ok = bool(staged.stat().st_size > 0)
        if not loose_ok:
            issues.append(f"loose staged AAS is empty: {staged}")
        if isinstance(expected_hash, str) and expected_hash and loose_hash.lower() != expected_hash.lower():
            loose_ok = False
            issues.append(
                f"loose staged AAS hash mismatch: expected {expected_hash.lower()}, got {loose_hash.lower()}"
            )
    else:
        issues.append(f"missing loose staged AAS: {staged}")
    audit["loose"] = loose_report

    archive_report: dict[str, Any] = {
        "exists": archive_member in archive_members,
        "status": "missing",
    }
    archive_ok = False
    if archive_member in archive_members:
        payload = archive_members[archive_member]
        member_hash = sha256_bytes(payload)
        archive_report.update({
            "status": "present",
            "size": len(payload),
            "sha256": member_hash,
        })
        archive_ok = bool(payload)
        if not archive_ok:
            issues.append(f"archived AAS member is empty: {archive_member}")
        if isinstance(expected_hash, str) and expected_hash and member_hash.lower() != expected_hash.lower():
            archive_ok = False
            issues.append(
                f"archived AAS hash mismatch for {archive_member}: "
                f"expected {expected_hash.lower()}, got {member_hash.lower()}"
            )
    elif require_archive_member:
        issues.append(f"missing archived AAS member: {archive_member}")
    audit["archive"] = archive_report

    represented = archive_ok or loose_ok
    if not represented:
        issues.append("AAS is not represented by a valid loose file or package archive member")
    audit["represented"] = represented
    audit["policy"] = "archive-required" if require_archive_member else "loose-or-archive"
    audit["status"] = "passed" if represented and (archive_ok or not require_archive_member) else "failed"
    return audit


def has_enabled_staged_output(entry: dict[str, Any]) -> bool:
    staged_output = entry.get("staged_output")
    return isinstance(staged_output, dict) and bool(staged_output.get("enabled"))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Audit q2aas staged AAS files against the local package payload."
    )
    parser.add_argument(
        "--report-json",
        default=".tmp/q2aas/stage-report.json",
        help="q2aas stage report produced by q2aas-stage-aas.",
    )
    parser.add_argument("--install-dir", default=".install", help="Install staging directory.")
    parser.add_argument("--base-game", default="basew", help="Base game directory name.")
    parser.add_argument("--archive-name", default="pak0.pkz", help="Base game package archive name.")
    parser.add_argument(
        "--require-archive-member",
        action="store_true",
        help="Fail unless each staged AAS also exists in the package archive.",
    )
    parser.add_argument(
        "--audit-report-json",
        default=".tmp/q2aas/package-audit-report.json",
        help="Output package audit JSON report.",
    )
    args = parser.parse_args()

    root = pathlib.Path(os.environ.get("MESON_SOURCE_ROOT", pathlib.Path.cwd())).resolve()
    stage_report_path = resolve_path(root, args.report_json).resolve()
    install_dir = resolve_path(root, args.install_dir).resolve()
    base_game_dir = install_dir / args.base_game
    archive_path = base_game_dir / args.archive_name
    audit_report_path = resolve_path(root, args.audit_report_json).resolve()

    stage_report = load_stage_report(stage_report_path)
    if not base_game_dir.is_dir():
        raise SystemExit(f"Base game stage directory not found: {base_game_dir}")

    archive_report, archive_members, archive_issues = load_archive(archive_path)
    maps = [entry for entry in stage_report.get("maps", []) if isinstance(entry, dict) and has_enabled_staged_output(entry)]

    audits = [
        audit_map(
            entry,
            base_game_dir,
            archive_members,
            require_archive_member=args.require_archive_member,
        )
        for entry in maps
    ]
    failed = [entry for entry in audits if entry["status"] != "passed"]
    if args.require_archive_member and archive_issues:
        failed.append({"id": "<archive>", "issues": archive_issues})

    report: dict[str, Any] = {
        "schema": PACKAGE_AUDIT_SCHEMA,
        "status": "passed" if not failed else "failed",
        "stage_report": str(stage_report_path),
        "install_dir": str(install_dir),
        "base_game": args.base_game,
        "base_game_dir": str(base_game_dir),
        "archive": archive_report,
        "archive_issues": archive_issues,
        "policy": "archive-required" if args.require_archive_member else "loose-or-archive",
        "map_count": len(audits),
        "failed_count": len(failed),
        "maps": audits,
    }

    audit_report_path.parent.mkdir(parents=True, exist_ok=True)
    audit_report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"[q2aas-package-audit] report: {audit_report_path}")

    for entry in audits:
        if entry["status"] == "passed":
            archive_state = entry["archive"]["status"]
            loose_state = entry["loose"]["status"]
            print(
                f"[q2aas-package-audit] {entry['id']}: represented "
                f"(loose={loose_state}, archive={archive_state})"
            )
        else:
            print(f"[q2aas-package-audit] {entry['id']}: failed", file=sys.stderr)
            for issue in entry["issues"]:
                print(f"[q2aas-package-audit]   {issue}", file=sys.stderr)

    if archive_issues and not args.require_archive_member:
        for issue in archive_issues:
            print(f"[q2aas-package-audit] warning: {issue}", file=sys.stderr)

    if failed:
        return 1

    print(f"[q2aas-package-audit] passed: {len(audits)} map(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
