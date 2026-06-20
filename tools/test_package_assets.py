#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "tools" / "package_assets.py"
REPO_ASSETS_DIR = REPO_ROOT / "assets"

sys.path.insert(0, str(REPO_ROOT / "tools"))
import package_assets  # noqa: E402
import refresh_install  # noqa: E402


BOTFILE_SUPPORT_MEMBERS = (
    "botfiles/chars.h",
    "botfiles/fw_items.c",
    "botfiles/fw_weap.c",
    "botfiles/inv.h",
    "botfiles/teamplay.h",
)
BOTFILE_PROFILE_SUFFIXES = ("_c.c", "_i.c", "_t.c", "_w.c")


def path_from_member(member: str) -> pathlib.Path:
    return pathlib.Path(*pathlib.PurePosixPath(member).parts)


def write_text_asset(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")


def write_botfile_fixture(assets_dir: pathlib.Path, bot_name: str = "smoke", *, include_script: bool = True) -> None:
    for member in BOTFILE_SUPPORT_MEMBERS:
        write_text_asset(assets_dir / path_from_member(member), f"// {member}\n")

    for suffix in BOTFILE_PROFILE_SUFFIXES:
        write_text_asset(
            assets_dir / "botfiles" / "bots" / f"{bot_name}{suffix}",
            f"// {bot_name}{suffix}\n",
        )

    if include_script:
        write_text_asset(
            assets_dir / "botfiles" / "scripts" / f"{bot_name}_s.c",
            f"// {bot_name} script\nbot {bot_name}\n",
        )


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class PackageAssetsTest(unittest.TestCase):
    def run_package_assets(
        self,
        assets_dir: pathlib.Path,
        install_dir: pathlib.Path,
        *,
        check: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--assets-dir",
                str(assets_dir),
                "--install-dir",
                str(install_dir),
                "--base-game",
                "basew",
                "--archive-name",
                "pak0.pkz",
            ],
            check=check,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def authored_botfile_members(self) -> list[str]:
        return package_assets.botfile_release_members(REPO_ASSETS_DIR)

    def test_botfiles_are_packaged_and_mirrored_loose(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            assets_dir = root / "assets"
            install_dir = root / "install"
            write_botfile_fixture(assets_dir)

            result = self.run_package_assets(assets_dir, install_dir)

            archive_path = install_dir / "basew" / "pak0.pkz"
            loose_path = install_dir / "basew" / "botfiles" / "bots" / "smoke_c.c"
            loose_script_path = install_dir / "basew" / "botfiles" / "scripts" / "smoke_s.c"
            profile = assets_dir / "botfiles" / "bots" / "smoke_c.c"
            script = assets_dir / "botfiles" / "scripts" / "smoke_s.c"
            self.assertIn("Validated botfile release payload: 10 package/loose file(s)", result.stdout)
            self.assertIn("Mirrored loose asset paths: botfiles", result.stdout)
            self.assertTrue(loose_path.is_file())
            self.assertTrue(loose_script_path.is_file())
            self.assertEqual(profile.read_text(encoding="ascii"), loose_path.read_text(encoding="ascii"))
            self.assertEqual(script.read_text(encoding="ascii"), loose_script_path.read_text(encoding="ascii"))
            with zipfile.ZipFile(archive_path) as archive:
                self.assertIn("botfiles/bots/smoke_c.c", archive.namelist())
                self.assertIn("botfiles/scripts/smoke_s.c", archive.namelist())

    def test_loose_mirror_removes_stale_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            assets_dir = root / "assets"
            install_dir = root / "install"
            write_botfile_fixture(assets_dir)

            self.run_package_assets(assets_dir, install_dir)
            stale_path = install_dir / "basew" / "botfiles" / "bots" / "stale.c"
            stale_path.write_text("{\nname Stale\n}\n", encoding="ascii")

            self.run_package_assets(assets_dir, install_dir)

            self.assertFalse(stale_path.exists())

    def test_authored_botfiles_are_packaged_and_mirrored_loose(self) -> None:
        expected_members = self.authored_botfile_members()
        self.assertIn("botfiles/scripts/smoke_s.c", expected_members)
        self.assertIn("botfiles/bots/smoke_c.c", expected_members)

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            install_dir = root / ".install"

            result = self.run_package_assets(REPO_ASSETS_DIR, install_dir)

            archive_path = install_dir / "basew" / "pak0.pkz"
            self.assertIn("Mirrored loose asset paths: botfiles", result.stdout)
            with zipfile.ZipFile(archive_path) as archive:
                archive_members = set(archive.namelist())

            staged_members = sorted(
                path.relative_to(install_dir / "basew").as_posix()
                for path in (install_dir / "basew" / "botfiles").rglob("*")
                if path.is_file()
            )

            self.assertEqual(expected_members, staged_members)
            for member in expected_members:
                source_path = REPO_ASSETS_DIR / path_from_member(member)
                loose_path = install_dir / "basew" / path_from_member(member)
                self.assertIn(member, archive_members)
                self.assertEqual(source_path.read_bytes(), loose_path.read_bytes())

    def test_missing_botfile_script_fails_release_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            assets_dir = root / "assets"
            install_dir = root / "install"
            write_botfile_fixture(assets_dir, include_script=False)

            result = self.run_package_assets(assets_dir, install_dir, check=False)

            self.assertNotEqual(0, result.returncode)
            self.assertIn("Invalid botfile release payload", result.stderr)
            self.assertIn("missing bot script companion: botfiles/scripts/smoke_s.c", result.stderr)

    def test_q2aas_tool_binary_is_rejected_from_asset_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            assets_dir = root / "assets"
            install_dir = root / "install"
            write_botfile_fixture(assets_dir)
            write_text_asset(assets_dir / "tools" / "worr_q2aas.exe", "binary placeholder\n")

            result = self.run_package_assets(assets_dir, install_dir, check=False)

            self.assertNotEqual(0, result.returncode)
            self.assertIn("q2aas/BSPC tool binaries are not packaged by default", result.stderr)
            self.assertIn("tools/worr_q2aas.exe", result.stderr)

    def test_botfile_archive_member_requirements_include_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            assets_dir = pathlib.Path(temp) / "assets"
            write_botfile_fixture(assets_dir)

            requirements = package_assets.botfile_archive_member_requirements(assets_dir)

            script = assets_dir / "botfiles" / "scripts" / "smoke_s.c"
            self.assertIn(f"botfiles/scripts/smoke_s.c={sha256_file(script)}", requirements)
            self.assertTrue(all("=" in requirement for requirement in requirements))


class RefreshInstallPackageValidationTest(unittest.TestCase):
    def test_q2aas_stage_report_becomes_hashed_archive_member_requirement(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            install_dir = root / ".install"
            report_path = root / "stage-report.json"
            expected_hash = "A" * 64
            report_path.write_text(
                json.dumps({
                    "maps": [
                        {
                            "id": "mm-rage",
                            "staged_output": {
                                "enabled": True,
                                "aas": "maps/mm-rage.aas",
                                "aas_sha256": expected_hash,
                            },
                        }
                    ]
                }),
                encoding="utf-8",
            )

            requirements = refresh_install.q2aas_archive_member_requirements(report_path, install_dir, "basew")

            self.assertEqual([f"maps/mm-rage.aas={expected_hash.lower()}"], requirements)

    def test_q2aas_stage_report_requires_valid_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            install_dir = root / ".install"
            report_path = root / "stage-report.json"
            report_path.write_text(
                json.dumps({
                    "maps": [
                        {
                            "id": "mm-rage",
                            "staged_output": {
                                "enabled": True,
                                "aas": "maps/mm-rage.aas",
                            },
                        }
                    ]
                }),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(SystemExit, "staged AAS SHA-256 is missing"):
                refresh_install.q2aas_archive_member_requirements(report_path, install_dir, "basew")

    def test_q2aas_stage_report_skips_disabled_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            install_dir = root / ".install"
            report_path = root / "stage-report.json"
            report_path.write_text(
                json.dumps({
                    "maps": [
                        {
                            "id": "pending",
                            "staged_output": {
                                "enabled": False,
                                "aas": "maps/pending.aas",
                            },
                        }
                    ]
                }),
                encoding="utf-8",
            )

            requirements = refresh_install.q2aas_archive_member_requirements(report_path, install_dir, "basew")

            self.assertEqual([], requirements)

    def test_release_notice_bundle_is_staged_and_validated(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            install_dir = pathlib.Path(temp) / ".install"
            install_dir.mkdir()

            refresh_install.stage_release_notice_bundle(REPO_ROOT, install_dir)

            for member in refresh_install.release_notice_destinations():
                path = install_dir / path_from_member(member)
                self.assertTrue(path.is_file(), member)
                self.assertGreater(path.stat().st_size, 0, member)
            refresh_install.validate_release_notice_bundle(install_dir)

    def test_q2aas_tool_binary_policy_rejects_staged_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            install_dir = pathlib.Path(temp) / ".install"
            tool_path = install_dir / "tools" / "worr_q2aas.exe"
            tool_path.parent.mkdir(parents=True)
            tool_path.write_text("binary placeholder\n", encoding="ascii")

            with self.assertRaisesRegex(SystemExit, "not part of default WORR binary releases"):
                refresh_install.validate_q2aas_tool_binary_policy(install_dir)


if __name__ == "__main__":
    unittest.main()
