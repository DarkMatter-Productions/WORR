#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from typing import Any


CLIENT_INCLUDE = [
    "worr*",
    "baseq2/*",
    "worr/pak0.pkz",
]

CLIENT_EXCLUDE = [
    "worr.ded*",
    "baseq2/.conhistory",
    "baseq2/logs/*",
]

SERVER_INCLUDE = [
    "worr.ded*",
    "baseq2/*",
    "worr/pak0.pkz",
]

SERVER_EXCLUDE = [
    "baseq2/cgame*",
    "baseq2/.conhistory",
    "baseq2/logs/*",
    "baseq2/shader_vkpt/*",
]


def with_payload_rules(target: dict[str, Any]) -> dict[str, Any]:
    client = dict(target["client"])
    server = dict(target["server"])

    client["include"] = list(CLIENT_INCLUDE)
    client["exclude"] = list(CLIENT_EXCLUDE)
    client["required_paths"] = [
        client["launch_exe"],
        "baseq2/cgame*",
        "baseq2/sgame*",
        "baseq2/worr-assets.pkz",
        "worr/pak0.pkz",
        "worr_update.json",
    ]
    client["forbidden_paths"] = [
        server["launch_exe"],
    ]

    server["include"] = list(SERVER_INCLUDE)
    server["exclude"] = list(SERVER_EXCLUDE)
    server["required_paths"] = [
        server["launch_exe"],
        "baseq2/sgame*",
        "baseq2/worr-assets.pkz",
        "worr/pak0.pkz",
    ]
    server["forbidden_paths"] = [
        client["launch_exe"],
        "worr_update.json",
        "worr_opengl*",
        "worr_rtx*",
        "worr_updater*",
        "worr_vulkan*",
        "baseq2/cgame*",
        "baseq2/shader_vkpt/*",
    ]

    hydrated = dict(target)
    hydrated["client"] = client
    hydrated["server"] = server
    return hydrated


TARGETS: list[dict[str, Any]] = [
    with_payload_rules({
        "platform_id": "windows-x86_64",
        "runner": "windows-latest",
        "os": "windows",
        "arch": "x86_64",
        "archive_format": "zip",
        "client": {
            "package_name": "worr-client-win64.zip",
            "manifest_name": "worr-client-win64.json",
            "launch_exe": "worr.exe",
        },
        "server": {
            "package_name": "worr-server-win64.zip",
            "manifest_name": "worr-server-win64.json",
            "launch_exe": "worr.ded.exe",
        },
        "installer": {
            "type": "msi",
            "name": "worr-win64.msi",
        },
        "autoupdater": {
            "mode": "native",
            "updater_asset": "worr_updater.exe",
            "config_asset": "worr_update.json",
        },
    }),
    with_payload_rules({
        "platform_id": "linux-x86_64",
        "runner": "ubuntu-latest",
        "os": "linux",
        "arch": "x86_64",
        "archive_format": "tar.gz",
        "client": {
            "package_name": "worr-client-linux-x86_64.tar.gz",
            "manifest_name": "worr-client-linux-x86_64.json",
            "launch_exe": "worr",
        },
        "server": {
            "package_name": "worr-server-linux-x86_64.tar.gz",
            "manifest_name": "worr-server-linux-x86_64.json",
            "launch_exe": "worr.ded",
        },
        "installer": None,
        "autoupdater": {
            "mode": "archive_sync",
            "updater_asset": None,
            "config_asset": "worr_update.json",
        },
    }),
    with_payload_rules({
        "platform_id": "macos-x86_64",
        "runner": "macos-15-intel",
        "os": "macos",
        "arch": "x86_64",
        "archive_format": "tar.gz",
        "client": {
            "package_name": "worr-client-macos-x86_64.tar.gz",
            "manifest_name": "worr-client-macos-x86_64.json",
            "launch_exe": "worr",
        },
        "server": {
            "package_name": "worr-server-macos-x86_64.tar.gz",
            "manifest_name": "worr-server-macos-x86_64.json",
            "launch_exe": "worr.ded",
        },
        "installer": None,
        "autoupdater": {
            "mode": "archive_sync",
            "updater_asset": None,
            "config_asset": "worr_update.json",
        },
    }),
]


def get_target(platform_id: str) -> dict[str, Any]:
    for target in TARGETS:
        if target["platform_id"] == platform_id:
            return target
    raise KeyError(f"Unknown platform id: {platform_id}")


def matrix_payload() -> dict[str, Any]:
    include = []
    for target in TARGETS:
        include.append(
            {
                "platform_id": target["platform_id"],
                "runner": target["runner"],
                "os": target["os"],
                "archive_format": target["archive_format"],
                "has_installer": bool(target["installer"]),
            }
        )
    return {"include": include}


def expected_asset_names(target: dict[str, Any]) -> list[str]:
    assets = [
        target["client"]["package_name"],
        target["client"]["manifest_name"],
        target["server"]["package_name"],
        target["server"]["manifest_name"],
    ]
    installer = target.get("installer")
    if installer:
        assets.append(installer["name"])
    return assets


def main() -> int:
    parser = argparse.ArgumentParser(description="WORR release target registry.")
    parser.add_argument("--matrix-json", action="store_true", help="Print GitHub matrix JSON")
    parser.add_argument("--platform", help="Platform id to print")
    parser.add_argument("--assets", action="store_true", help="Print expected asset names for --platform")
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON output")
    args = parser.parse_args()

    indent = 2 if args.pretty else None

    if args.matrix_json:
        print(json.dumps(matrix_payload(), indent=indent))
        return 0

    if args.platform:
        target = get_target(args.platform)
        if args.assets:
            for name in expected_asset_names(target):
                print(name)
        else:
            print(json.dumps(target, indent=indent))
        return 0

    print(json.dumps({"targets": TARGETS}, indent=indent))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
