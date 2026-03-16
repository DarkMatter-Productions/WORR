#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from typing import Any


BASE_GAME = "basew"


def runtime_binary_name(stem: str, arch: str, os_name: str) -> str:
    suffix = ".exe" if os_name == "windows" else ""
    return f"{stem}_{arch}{suffix}"


def runtime_binary_glob(stem: str, arch: str) -> str:
    return f"{stem}_{arch}*"


def client_required_paths(target: dict[str, Any]) -> list[str]:
    required = [
        target["client"]["launch_exe"],
        f"{BASE_GAME}/cgame*",
        f"{BASE_GAME}/sgame*",
        f"{BASE_GAME}/pak0.pkz",
        "worr_update.json",
    ]
    updater_asset = target.get("autoupdater", {}).get("updater_asset")
    if updater_asset:
        required.append(updater_asset)
    return required


def server_required_paths(target: dict[str, Any]) -> list[str]:
    return [
        target["server"]["launch_exe"],
        f"{BASE_GAME}/sgame*",
        f"{BASE_GAME}/pak0.pkz",
    ]


def client_forbidden_paths(target: dict[str, Any]) -> list[str]:
    return [
        target["server"]["launch_exe"],
        "baseq2/*",
        "worr/*",
        "bin/*",
        ".release/*",
        ".release/**/*",
    ]


def server_forbidden_paths(target: dict[str, Any]) -> list[str]:
    return [
        target["client"]["launch_exe"],
        "worr_update.json",
        "worr_opengl*",
        "worr_rtx*",
        "worr_updater*",
        "worr_vulkan*",
        f"{BASE_GAME}/cgame*",
        f"{BASE_GAME}/shader_vkpt/*",
        "baseq2/*",
        "worr/*",
        "bin/*",
        ".release/*",
        ".release/**/*",
    ]


def build_target(
    *,
    platform_id: str,
    runner: str,
    os_name: str,
    arch: str,
    archive_format: str,
    client_package_name: str,
    client_manifest_name: str,
    server_package_name: str,
    server_manifest_name: str,
    installer: dict[str, Any] | None,
    autoupdater: dict[str, Any],
) -> dict[str, Any]:
    client_launch = runtime_binary_name("worr", arch, os_name)
    server_launch = runtime_binary_name("worr_ded", arch, os_name)

    target: dict[str, Any] = {
        "platform_id": platform_id,
        "runner": runner,
        "os": os_name,
        "arch": arch,
        "archive_format": archive_format,
        "client": {
            "package_name": client_package_name,
            "manifest_name": client_manifest_name,
            "launch_exe": client_launch,
            "include": [
                runtime_binary_glob("worr", arch),
                "worr_opengl_*",
                "worr_vulkan_*",
                "worr_rtx_*",
                "worr_update.json",
                f"{BASE_GAME}/*",
            ],
            "exclude": [
                runtime_binary_glob("worr_ded", arch),
                f"{BASE_GAME}/.conhistory",
                f"{BASE_GAME}/logs/*",
            ],
        },
        "server": {
            "package_name": server_package_name,
            "manifest_name": server_manifest_name,
            "launch_exe": server_launch,
            "include": [
                runtime_binary_glob("worr_ded", arch),
                f"{BASE_GAME}/*",
            ],
            "exclude": [
                runtime_binary_glob("worr", arch),
                "worr_opengl_*",
                "worr_vulkan_*",
                "worr_rtx_*",
                "worr_updater_*",
                "worr_update.json",
                f"{BASE_GAME}/cgame*",
                f"{BASE_GAME}/.conhistory",
                f"{BASE_GAME}/logs/*",
                f"{BASE_GAME}/shader_vkpt/*",
            ],
        },
        "installer": installer,
        "autoupdater": autoupdater,
    }

    updater_asset = autoupdater.get("updater_asset")
    if updater_asset:
        target["client"]["include"].append(updater_asset)

    target["client"]["required_paths"] = client_required_paths(target)
    target["client"]["forbidden_paths"] = client_forbidden_paths(target)
    target["server"]["required_paths"] = server_required_paths(target)
    target["server"]["forbidden_paths"] = server_forbidden_paths(target)
    return target


TARGETS: list[dict[str, Any]] = [
    build_target(
        platform_id="windows-x86_64",
        runner="windows-latest",
        os_name="windows",
        arch="x86_64",
        archive_format="zip",
        client_package_name="worr-client-win64.zip",
        client_manifest_name="worr-client-win64.json",
        server_package_name="worr-server-win64.zip",
        server_manifest_name="worr-server-win64.json",
        installer={
            "type": "msi",
            "name": "worr-win64.msi",
        },
        autoupdater={
            "mode": "native",
            "updater_asset": runtime_binary_name("worr_updater", "x86_64", "windows"),
            "config_asset": "worr_update.json",
        },
    ),
    build_target(
        platform_id="linux-x86_64",
        runner="ubuntu-latest",
        os_name="linux",
        arch="x86_64",
        archive_format="tar.gz",
        client_package_name="worr-client-linux-x86_64.tar.gz",
        client_manifest_name="worr-client-linux-x86_64.json",
        server_package_name="worr-server-linux-x86_64.tar.gz",
        server_manifest_name="worr-server-linux-x86_64.json",
        installer=None,
        autoupdater={
            "mode": "archive_sync",
            "updater_asset": None,
            "config_asset": "worr_update.json",
        },
    ),
    build_target(
        platform_id="macos-x86_64",
        runner="macos-15-intel",
        os_name="macos",
        arch="x86_64",
        archive_format="tar.gz",
        client_package_name="worr-client-macos-x86_64.tar.gz",
        client_manifest_name="worr-client-macos-x86_64.json",
        server_package_name="worr-server-macos-x86_64.tar.gz",
        server_manifest_name="worr-server-macos-x86_64.json",
        installer=None,
        autoupdater={
            "mode": "archive_sync",
            "updater_asset": None,
            "config_asset": "worr_update.json",
        },
    ),
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
