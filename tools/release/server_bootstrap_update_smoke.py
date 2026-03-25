#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time


def load_json(path: pathlib.Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def find_update_manifest(release_root: pathlib.Path) -> pathlib.Path:
    matches = sorted(release_root.glob("worr-server-*-update.json"))
    if len(matches) != 1:
        raise SystemExit(f"Expected exactly one server update manifest in {release_root}, found {len(matches)}")
    return matches[0]


def write_pending_state(
    install_dir: pathlib.Path, update_manifest: dict, update_manifest_name: str, port: int
) -> pathlib.Path:
    state_path = install_dir / "worr_update_state.json"
    package = update_manifest.get("package", {})
    state = {
        "schema_version": 1,
        "last_result": "update_available",
        "last_successful_check_utc": "2026-03-25T12:00:00Z",
        "pending_update": {
            "version": update_manifest["version"],
            "tag": f"v{update_manifest['version']}",
            "role": update_manifest["role"],
            "launch_exe": update_manifest["launch_exe"],
            "engine_library": update_manifest["engine_library"],
            "update_manifest_name": update_manifest_name,
            "update_package_name": package["name"],
            "local_manifest_name": update_manifest["local_manifest_name"],
            "manifest_url": f"http://127.0.0.1:{port}/{update_manifest_name}",
            "package_url": f"http://127.0.0.1:{port}/{package['name']}",
            "manifest": update_manifest,
        },
    }
    state_path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    return state_path


def current_version(install_dir: pathlib.Path) -> str:
    manifest = load_json(install_dir / "worr_install_manifest.json")
    return manifest["version"]


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke test the public dedicated bootstrap updater path.")
    parser.add_argument("--install-dir", required=True, help="Prepared server install root")
    parser.add_argument("--release-root", required=True, help="Release artifact root with server update payload")
    parser.add_argument("--action", choices=("install", "exit"), required=True, help="Whether to approve or defer")
    parser.add_argument("--port", type=int, default=8777, help="Local HTTP port")
    parser.add_argument("--timeout", type=int, default=120, help="Overall timeout in seconds")
    parser.add_argument("--wait", type=int, default=60, help="Engine +wait frame count")
    args = parser.parse_args()

    install_dir = pathlib.Path(args.install_dir).resolve()
    release_root = pathlib.Path(args.release_root).resolve()
    if not install_dir.is_dir():
        raise SystemExit(f"Install dir not found: {install_dir}")
    if not release_root.is_dir():
        raise SystemExit(f"Release root not found: {release_root}")

    update_manifest_path = find_update_manifest(release_root)
    update_manifest = load_json(update_manifest_path)
    before_version = current_version(install_dir)
    expected_version = update_manifest["version"] if args.action == "install" else before_version
    write_pending_state(install_dir, update_manifest, update_manifest_path.name, args.port)

    trace_path = pathlib.Path(tempfile.gettempdir()) / "worr-bootstrap-trace.log"
    trace_path.write_text("", encoding="utf-8")

    server = subprocess.Popen(
        [sys.executable, "-m", "http.server", str(args.port), "--bind", "127.0.0.1"],
        cwd=release_root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )

    launch_exe = install_dir / update_manifest["launch_exe"]
    command = [str(launch_exe), "+set", "basedir", str(install_dir), "+wait", str(args.wait), "+quit"]
    env = dict(os.environ)
    env["WORR_BOOTSTRAP_TRACE"] = "1"
    answer = "i\n" if args.action == "install" else "e\n"

    try:
        subprocess.run(
            command,
            cwd=install_dir,
            input=answer,
            text=True,
            env=env,
            check=True,
            timeout=args.timeout,
        )

        deadline = time.time() + args.timeout
        while time.time() < deadline:
            if current_version(install_dir) == expected_version:
                print(f"{args.action} smoke passed: {before_version} -> {expected_version}")
                print(f"trace: {trace_path}")
                return 0
            time.sleep(1)

        print(f"{args.action} smoke failed: manifest version stayed at {current_version(install_dir)}", file=sys.stderr)
        if trace_path.is_file():
            print(trace_path.read_text(encoding="utf-8"), file=sys.stderr)
        return 1
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
