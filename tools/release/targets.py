#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from typing import Any


BASE_GAME = "basew"
LOCAL_INSTALL_MANIFEST = "worr_install_manifest.json"


def binary_name(stem: str, arch: str, os_name: str) -> str:
    suffix = ".exe" if os_name == "windows" else ""
    return f"{stem}_{arch}{suffix}"


def binary_glob(stem: str, arch: str) -> str:
    return f"{stem}_{arch}*"


def engine_library_name(stem: str, arch: str, os_name: str) -> str:
    suffix = {
        "windows": ".dll",
        "linux": ".so",
        "macos": ".dylib",
    }[os_name]
    return f"{stem}_engine_{arch}{suffix}"


def engine_library_glob(stem: str, arch: str) -> str:
    return f"{stem}_engine_{arch}*"


def update_asset_name(prefix: str, platform_stub: str, ext: str) -> str:
    return f"{prefix}-{platform_stub}-update.{ext}"


def client_required_paths(target: dict[str, Any]) -> list[str]:
    required = [
        target["client"]["launch_exe"],
        target["client"]["engine_library"],
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
    required = [
        target["server"]["launch_exe"],
        target["server"]["engine_library"],
        "worr_update.json",
        f"{BASE_GAME}/sgame*",
        f"{BASE_GAME}/pak0.pkz",
    ]
    updater_asset = target.get("autoupdater", {}).get("updater_asset")
    if updater_asset:
        required.append(updater_asset)
    return required


def client_forbidden_paths(target: dict[str, Any]) -> list[str]:
    return [
        target["server"]["launch_exe"],
        target["server"]["engine_library"],
        "baseq2/*",
        "worr/*",
        ".release/*",
        ".release/**/*",
    ]


def server_forbidden_paths(target: dict[str, Any]) -> list[str]:
    return [
        target["client"]["launch_exe"],
        target["client"]["engine_library"],
        f"{BASE_GAME}/cgame*",
        f"{BASE_GAME}/shader_vkpt/*",
        "baseq2/*",
        "worr/*",
        ".release/*",
        ".release/**/*",
    ]


def full_install_required_paths(target: dict[str, Any]) -> list[str]:
    required = [
        target["client"]["launch_exe"],
        target["client"]["engine_library"],
        target["server"]["launch_exe"],
        target["server"]["engine_library"],
        f"{BASE_GAME}/cgame*",
        f"{BASE_GAME}/sgame*",
        f"{BASE_GAME}/pak0.pkz",
        "worr_update.json",
    ]
    updater_asset = target.get("autoupdater", {}).get("updater_asset")
    if updater_asset:
        required.append(updater_asset)
    return required


def full_install_forbidden_paths() -> list[str]:
    return [
        "baseq2/*",
        "worr/*",
        ".release/*",
        ".release/**/*",
    ]


def build_target(
    *,
    platform_id: str,
    platform_stub: str,
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
    client_launch = binary_name("worr", arch, os_name)
    server_launch = binary_name("worr_ded", arch, os_name)
    client_engine = engine_library_name("worr", arch, os_name)
    server_engine = engine_library_name("worr_ded", arch, os_name)

    updater_asset = binary_name("worr_updater", arch, os_name)
    target: dict[str, Any] = {
        "platform_id": platform_id,
        "platform_stub": platform_stub,
        "runner": runner,
        "os": os_name,
        "arch": arch,
        "archive_format": archive_format,
        "client": {
            "role": "client",
            "package_name": client_package_name,
            "manifest_name": client_manifest_name,
            "update_package_name": update_asset_name("worr-client", platform_stub, "zip"),
            "update_manifest_name": update_asset_name("worr-client", platform_stub, "json"),
            "launch_exe": client_launch,
            "engine_library": client_engine,
            "local_manifest_name": LOCAL_INSTALL_MANIFEST,
            "include": [
                binary_glob("worr", arch),
                engine_library_glob("worr", arch),
                binary_glob("worr_updater", arch),
                "worr_opengl_*",
                "worr_vulkan_*",
                "worr_rtx_*",
                "worr_update.json",
                f"{BASE_GAME}/*",
            ],
            "exclude": [
                binary_glob("worr_ded", arch),
                engine_library_glob("worr_ded", arch),
                f"{BASE_GAME}/.conhistory",
                f"{BASE_GAME}/logs/*",
            ],
        },
        "server": {
            "role": "server",
            "package_name": server_package_name,
            "manifest_name": server_manifest_name,
            "update_package_name": update_asset_name("worr-server", platform_stub, "zip"),
            "update_manifest_name": update_asset_name("worr-server", platform_stub, "json"),
            "launch_exe": server_launch,
            "engine_library": server_engine,
            "local_manifest_name": LOCAL_INSTALL_MANIFEST,
            "include": [
                binary_glob("worr_ded", arch),
                engine_library_glob("worr_ded", arch),
                binary_glob("worr_updater", arch),
                "worr_update.json",
                f"{BASE_GAME}/*",
            ],
            "exclude": [
                binary_glob("worr", arch),
                engine_library_glob("worr", arch),
                "worr_opengl_*",
                "worr_vulkan_*",
                "worr_rtx_*",
                f"{BASE_GAME}/cgame*",
                f"{BASE_GAME}/.conhistory",
                f"{BASE_GAME}/logs/*",
                f"{BASE_GAME}/shader_vkpt/*",
            ],
        },
        "installer": installer,
        "autoupdater": {
            **autoupdater,
            "mode": "bootstrap_v1",
            "updater_asset": updater_asset,
        },
    }

    target["client"]["required_paths"] = client_required_paths(target)
    target["client"]["forbidden_paths"] = client_forbidden_paths(target)
    target["server"]["required_paths"] = server_required_paths(target)
    target["server"]["forbidden_paths"] = server_forbidden_paths(target)

    full_required = full_install_required_paths(target)
    full_forbidden = full_install_forbidden_paths()
    full_include = [
        binary_glob("worr", arch),
        binary_glob("worr_ded", arch),
        engine_library_glob("worr", arch),
        engine_library_glob("worr_ded", arch),
        binary_glob("worr_updater", arch),
        "worr_opengl_*",
        "worr_vulkan_*",
        "worr_rtx_*",
        "worr_update.json",
        f"{BASE_GAME}/*",
    ]
    full_exclude = [
        f"{BASE_GAME}/.conhistory",
        f"{BASE_GAME}/logs/*",
    ]

    for role in ("client", "server"):
        target[role]["update_include"] = list(full_include)
        target[role]["update_exclude"] = list(full_exclude)
        target[role]["update_required_paths"] = list(full_required)
        target[role]["update_forbidden_paths"] = list(full_forbidden)

    return target


TARGETS: list[dict[str, Any]] = [
    build_target(
        platform_id="windows-x86_64",
        platform_stub="win64",
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
            "config_asset": "worr_update.json",
        },
    ),
    build_target(
        platform_id="linux-x86_64",
        platform_stub="linux-x86_64",
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
            "config_asset": "worr_update.json",
        },
    ),
    build_target(
        platform_id="macos-x86_64",
        platform_stub="macos-x86_64",
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
        target["client"]["update_package_name"],
        target["client"]["update_manifest_name"],
        target["server"]["package_name"],
        target["server"]["manifest_name"],
        target["server"]["update_package_name"],
        target["server"]["update_manifest_name"],
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
