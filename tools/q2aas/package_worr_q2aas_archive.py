#!/usr/bin/env python3
"""Package staged q2aas AAS files into the local base game archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import zipfile
from typing import Any


PACKAGE_ARCHIVE_SCHEMA = "worr-q2aas-package-archive-v1"
FIXED_ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


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


def resolve_path(root: pathlib.Path, value: str) -> pathlib.Path:
    path = pathlib.Path(value)
    if path.is_absolute():
        return path
    return root / path


def is_under(path: pathlib.Path, root: pathlib.Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


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


def has_enabled_staged_output(entry: dict[str, Any]) -> bool:
    staged_output = entry.get("staged_output")
    return isinstance(staged_output, dict) and bool(staged_output.get("enabled"))


def staged_aas_path(staged_value: Any, base_game_dir: pathlib.Path) -> pathlib.Path | None:
    if not isinstance(staged_value, str) or not staged_value:
        return None
    staged = pathlib.Path(staged_value)
    if not staged.is_absolute():
        staged = base_game_dir / staged
    return staged.resolve()


def archive_member_for(staged_aas: pathlib.Path, base_game_dir: pathlib.Path) -> str:
    if is_under(staged_aas, base_game_dir):
        return staged_aas.relative_to(base_game_dir).as_posix()
    return f"maps/{staged_aas.name}"


def load_existing_archive(path: pathlib.Path) -> tuple[dict[str, bytes], list[str]]:
    members: dict[str, bytes] = {}
    order: list[str] = []
    if not path.is_file():
        return members, order

    try:
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                name = info.filename.replace("\\", "/")
                if name.endswith("/"):
                    continue
                if name in members:
                    raise SystemExit(f"Duplicate archive member in {path}: {name}")
                members[name] = archive.read(info.filename)
                order.append(name)
    except zipfile.BadZipFile as exc:
        raise SystemExit(f"Package archive is not a readable zip/pkz file: {path}: {exc}") from exc
    except OSError as exc:
        raise SystemExit(f"Unable to read package archive {path}: {exc}") from exc
    return members, order


def collect_aas_entries(stage_report: dict[str, Any], base_game_dir: pathlib.Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    failures: list[str] = []

    for map_entry in stage_report.get("maps", []):
        if not isinstance(map_entry, dict) or not has_enabled_staged_output(map_entry):
            continue

        map_id = map_entry.get("id")
        if not isinstance(map_id, str) or not map_id:
            map_id = "<unnamed>"

        staged_output = map_entry.get("staged_output")
        assert isinstance(staged_output, dict)
        staged_aas = staged_aas_path(staged_output.get("aas"), base_game_dir)
        expected_hash = staged_output.get("aas_sha256") or map_entry.get("aas_sha256")

        if staged_aas is None:
            failures.append(f"{map_id}: staged_output.aas is missing")
            continue
        if staged_aas.suffix.lower() != ".aas":
            failures.append(f"{map_id}: staged output is not an .aas file: {staged_aas}")
            continue
        if not staged_aas.is_file():
            failures.append(f"{map_id}: staged AAS file is missing: {staged_aas}")
            continue
        payload = staged_aas.read_bytes()
        payload_hash = sha256_bytes(payload)
        if not payload:
            failures.append(f"{map_id}: staged AAS file is empty: {staged_aas}")
            continue
        if isinstance(expected_hash, str) and expected_hash and payload_hash.lower() != expected_hash.lower():
            failures.append(
                f"{map_id}: staged AAS hash mismatch: expected {expected_hash.lower()}, got {payload_hash.lower()}"
            )
            continue

        entries.append({
            "id": map_id,
            "source": str(staged_aas),
            "archive_member": archive_member_for(staged_aas, base_game_dir),
            "size": len(payload),
            "sha256": payload_hash,
            "payload": payload,
        })

    if failures:
        for failure in failures:
            print(f"[q2aas-package-archive] {failure}")
        raise SystemExit(1)
    return entries


def zipinfo_for(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, FIXED_ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    return info


def write_archive(path: pathlib.Path, temp_path: pathlib.Path, order: list[str], members: dict[str, bytes]) -> None:
    temp_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(temp_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        seen: set[str] = set()
        for name in order:
            if name in seen or name not in members:
                continue
            archive.writestr(zipinfo_for(name), members[name])
            seen.add(name)
        for name in sorted(members):
            if name in seen:
                continue
            archive.writestr(zipinfo_for(name), members[name])
            seen.add(name)
    shutil.move(str(temp_path), path)


def main() -> int:
    parser = argparse.ArgumentParser(description="Package staged q2aas AAS output into pak0.pkz.")
    parser.add_argument(
        "--report-json",
        default=".tmp/q2aas/stage-report.json",
        help="q2aas stage report produced by q2aas-stage-aas.",
    )
    parser.add_argument("--install-dir", default=".install", help="Install staging directory.")
    parser.add_argument("--base-game", default="basew", help="Base game directory name.")
    parser.add_argument("--archive-name", default="pak0.pkz", help="Base game package archive name.")
    parser.add_argument(
        "--create-archive",
        action="store_true",
        help="Create the package archive when it does not already exist.",
    )
    parser.add_argument(
        "--package-report-json",
        default=".tmp/q2aas/package-archive-report.json",
        help="Output package archive JSON report.",
    )
    args = parser.parse_args()

    root = pathlib.Path(os.environ.get("MESON_SOURCE_ROOT", pathlib.Path.cwd())).resolve()
    stage_report_path = resolve_path(root, args.report_json).resolve()
    install_dir = resolve_path(root, args.install_dir).resolve()
    base_game_dir = install_dir / args.base_game
    archive_path = base_game_dir / args.archive_name
    package_report_path = resolve_path(root, args.package_report_json).resolve()
    temp_archive_path = root / ".tmp" / "q2aas" / "package-archive" / f"{args.archive_name}.tmp"

    stage_report = load_stage_report(stage_report_path)
    if not base_game_dir.is_dir():
        raise SystemExit(f"Base game stage directory not found: {base_game_dir}")
    if not archive_path.is_file() and not args.create_archive:
        raise SystemExit(f"Package archive not found: {archive_path}")

    before_report: dict[str, Any] = {
        "path": str(archive_path),
        "exists": archive_path.is_file(),
    }
    if archive_path.is_file():
        before_report.update({
            "size": archive_path.stat().st_size,
            "sha256": sha256_file(archive_path),
        })

    existing_members, order = load_existing_archive(archive_path)
    aas_entries = collect_aas_entries(stage_report, base_game_dir)
    if not aas_entries:
        raise SystemExit("No staged q2aas AAS entries found in stage report")

    replaced = 0
    added = 0
    staged_members: list[dict[str, Any]] = []
    for entry in aas_entries:
        member = entry["archive_member"]
        action = "replaced" if member in existing_members else "added"
        if member in existing_members:
            replaced += 1
        else:
            added += 1
            order.append(member)
        existing_members[member] = entry["payload"]
        staged_members.append({
            "id": entry["id"],
            "source": entry["source"],
            "archive_member": member,
            "size": entry["size"],
            "sha256": entry["sha256"],
            "action": action,
        })

    write_archive(archive_path, temp_archive_path, order, existing_members)
    after_report = {
        "path": str(archive_path),
        "exists": archive_path.is_file(),
        "size": archive_path.stat().st_size,
        "sha256": sha256_file(archive_path),
        "member_count": len(existing_members),
    }

    report: dict[str, Any] = {
        "schema": PACKAGE_ARCHIVE_SCHEMA,
        "status": "passed",
        "stage_report": str(stage_report_path),
        "install_dir": str(install_dir),
        "base_game": args.base_game,
        "base_game_dir": str(base_game_dir),
        "archive_before": before_report,
        "archive_after": after_report,
        "added_count": added,
        "replaced_count": replaced,
        "maps": staged_members,
    }

    package_report_path.parent.mkdir(parents=True, exist_ok=True)
    package_report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(f"[q2aas-package-archive] archive: {archive_path}")
    for entry in staged_members:
        print(
            f"[q2aas-package-archive] {entry['id']}: {entry['archive_member']} "
            f"({entry['size']} bytes, sha256 {entry['sha256']})"
        )
    print(f"[q2aas-package-archive] report: {package_report_path}")
    print(f"[q2aas-package-archive] added={added} replaced={replaced}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
