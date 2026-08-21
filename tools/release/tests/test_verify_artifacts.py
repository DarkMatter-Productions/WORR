from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import pathlib
import stat
import struct
import sys
import tarfile
import tempfile
import unittest
import zipfile
from unittest import mock

from tools.release import verify_artifacts


def minimal_compound_file(root_clsid: bytes = verify_artifacts.MSI_ROOT_CLSID_LE) -> bytes:
    header = bytearray(512)
    header[:8] = verify_artifacts.MSI_COMPOUND_FILE_SIGNATURE
    struct.pack_into("<H", header, 24, 0x003E)
    struct.pack_into("<H", header, 26, 3)
    struct.pack_into("<H", header, 28, 0xFFFE)
    struct.pack_into("<H", header, 30, 9)
    struct.pack_into("<H", header, 32, 6)
    struct.pack_into("<I", header, 40, 0)
    struct.pack_into("<I", header, 44, 1)
    struct.pack_into("<I", header, 48, 1)
    struct.pack_into("<I", header, 56, 0x1000)
    struct.pack_into("<I", header, 60, verify_artifacts.CFB_END_OF_CHAIN)
    struct.pack_into("<I", header, 64, 0)
    struct.pack_into("<I", header, 68, verify_artifacts.CFB_END_OF_CHAIN)
    struct.pack_into("<I", header, 72, 0)
    struct.pack_into("<109I", header, 76, 0, *([verify_artifacts.CFB_FREE_SECTOR] * 108))

    fat = bytearray(512)
    struct.pack_into("<128I", fat, 0, *([verify_artifacts.CFB_FREE_SECTOR] * 128))
    struct.pack_into("<I", fat, 0, 0xFFFFFFFD)
    struct.pack_into("<I", fat, 4, verify_artifacts.CFB_END_OF_CHAIN)

    directory = bytearray(512)
    root_name = "Root Entry\0".encode("utf-16le")
    directory[: len(root_name)] = root_name
    struct.pack_into("<H", directory, 64, len(root_name))
    directory[66] = 5
    directory[67] = 1
    directory[80:96] = root_clsid
    return bytes(header + fat + directory)


def msi_payload_fixture() -> tuple[dict, dict[str, bytes]]:
    payloads = {
        "worr.exe": b"launcher payload",
        "worr_engine_x86_64.dll": b"engine payload",
        "basew/pak0.pkz": b"game payload",
        "worr_update.json": b'{"channel":"stable"}',
    }
    files = [
        {
            "path": path,
            "sha256": hashlib.sha256(payload).hexdigest(),
            "size": len(payload),
            "mode": 0o755 if path.endswith((".exe", ".dll")) else 0o644,
        }
        for path, payload in payloads.items()
    ]
    manifest = {
        "schema_version": 3,
        "version": "1.2.3",
        "repo": "example/WORR",
        "channel": "stable",
        "role": "client",
        "generated_at_utc": "2026-08-03T12:00:00Z",
        "launch_exe": "worr.exe",
        "engine_library": "worr_engine_x86_64.dll",
        "local_manifest_name": "worr_install_manifest.json",
        "files": files,
        "platform": {
            "id": "windows-x86_64",
            "os": "windows",
            "arch": "x86_64",
        },
        "build": {"id": "build-1", "commit_sha": "a" * 40},
        "package": {
            "name": "worr-client-win64.zip",
            "format": "zip",
            "sha256": "b" * 64,
            "size": 123,
        },
    }
    local_manifest = dict(manifest)
    local_manifest.pop("package")
    local_manifest["generated_at_utc"] = "2026-08-03T12:00:01Z"
    local_manifest["install_state"] = True
    payloads["worr_install_manifest.json"] = json.dumps(
        local_manifest,
        indent=2,
    ).encode("utf-8")
    return manifest, payloads


class VerifyArtifactsTests(unittest.TestCase):
    @staticmethod
    def canonical_metadata(target: dict) -> dict:
        return {
            "schema_version": 3,
            "platform_id": target["platform_id"],
            "platform_stub": target["platform_stub"],
            "os": target["os"],
            "arch": target["arch"],
            "archive_format": target["archive_format"],
            "autoupdater": target["autoupdater"],
            "repo": "example/WORR",
            "channel": "stable",
            "version": "1.2.3",
            "commit_sha": "a" * 40,
            "build_id": f"test-{target['platform_id']}",
            "roles": verify_artifacts.expected_role_metadata(target),
            "artifacts": verify_artifacts.expected_artifacts(target),
        }

    def test_composer_metadata_contract_rejects_swapped_semantics(self) -> None:
        target = verify_artifacts.get_target("windows-x86_64")
        metadata = self.canonical_metadata(target)
        metadata["platform_stub"] = "corrupt-stub"
        metadata["archive_format"] = "corrupt-format"
        metadata["autoupdater"] = {}
        metadata["roles"]["client"]["update_package_name"] = target["server"]["update_package_name"]
        metadata["artifacts"][0]["kind"] = "manifest"
        metadata["artifacts"][0]["role"] = "server"
        metadata["artifacts"][0]["variant"] = "update"
        metadata["artifacts"].append(dict(metadata["artifacts"][0]))
        metadata["artifacts"].append(
            {"name": "unexpected.zip", "kind": "archive", "role": "client", "variant": "manual"}
        )
        failures: list[str] = []

        verify_artifacts.validate_metadata_contract(target, metadata, failures)

        self.assertTrue(any("platform_stub mismatch" in failure for failure in failures))
        self.assertTrue(any("archive_format mismatch" in failure for failure in failures))
        self.assertTrue(any("autoupdater contract mismatch" in failure for failure in failures))
        self.assertTrue(any("client update_package_name mismatch" in failure for failure in failures))
        self.assertTrue(any("artifact worr-client-win64.zip kind mismatch" in failure for failure in failures))
        self.assertTrue(any("duplicate artifact worr-client-win64.zip" in failure for failure in failures))
        self.assertTrue(any("unexpected artifact unexpected.zip" in failure for failure in failures))

    def test_unexpected_and_duplicate_metadata_files_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            primary = root / "primary"
            duplicate = root / "duplicate"
            unexpected = root / "unexpected"
            primary.mkdir()
            duplicate.mkdir()
            unexpected.mkdir()
            (unexpected / "metadata-evil.json").write_text("{}", encoding="utf-8")
            (primary / "metadata-windows-x86_64.json").write_text("{}", encoding="utf-8")
            failures: list[str] = []

            discovered = verify_artifacts.discover_metadata_files(root, failures)

            self.assertEqual(
                primary / "metadata-windows-x86_64.json",
                discovered.get("windows-x86_64"),
            )
            self.assertTrue(
                any(
                    "unexpected metadata file unexpected/metadata-evil.json" in failure
                    for failure in failures
                )
            )

            (unexpected / "metadata-evil.json").unlink()
            (duplicate / "METADATA-WINDOWS-X86_64.JSON").write_text(
                "not JSON and must not be read",
                encoding="utf-8",
            )
            failures = []
            with mock.patch.object(
                pathlib.Path,
                "open",
                side_effect=AssertionError("metadata content was read during layout preflight"),
            ):
                discovered = verify_artifacts.discover_metadata_files(root, failures)

            self.assertNotIn("windows-x86_64", discovered)
            self.assertTrue(
                any("case-insensitive collision" in failure for failure in failures)
            )

            (primary / "metadata-windows-x86_64.json").unlink()
            failures = []
            with mock.patch.object(
                pathlib.Path,
                "open",
                side_effect=AssertionError("noncanonical metadata content was read"),
            ):
                discovered = verify_artifacts.discover_metadata_files(root, failures)

        self.assertNotIn("windows-x86_64", discovered)
        self.assertTrue(any("canonical lowercase spelling" in failure for failure in failures))

    def test_artifact_layout_rejects_reparse_points_and_hardlinks(self) -> None:
        reparse_status = type(
            "ReparseStatus",
            (),
            {
                "st_mode": stat.S_IFREG,
                "st_file_attributes": getattr(
                    stat,
                    "FILE_ATTRIBUTE_REPARSE_POINT",
                    0x400,
                ),
            },
        )()
        self.assertTrue(verify_artifacts._is_link_or_reparse(reparse_status))

        directory_status = type(
            "DirectoryStatus",
            (),
            {"st_mode": stat.S_IFDIR, "st_file_attributes": 0},
        )()
        synthetic_root = pathlib.Path(tempfile.gettempdir()) / "redirect" / "artifacts"

        def synthetic_lstat(path: pathlib.Path) -> object:
            if path.name == "redirect":
                return reparse_status
            return directory_status

        with (
            mock.patch.object(
                type(pathlib.Path()),
                "lstat",
                autospec=True,
                side_effect=synthetic_lstat,
            ),
            self.assertRaisesRegex(
                verify_artifacts.ArtifactLayoutError,
                "artifacts root.*link or reparse point",
            ),
        ):
            verify_artifacts._require_non_redirecting_directory(
                synthetic_root,
                "artifacts root",
            )

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            source = root / "source.json"
            source.write_text("not read", encoding="utf-8")

            if hasattr(os, "link"):
                metadata_path = artifacts_root / "metadata-linux-x86_64.json"
                os.link(source, metadata_path)
                with (
                    mock.patch.object(
                        pathlib.Path,
                        "open",
                        side_effect=AssertionError("hard-linked metadata was read"),
                    ),
                    self.assertRaisesRegex(
                        verify_artifacts.ArtifactLayoutError,
                        "hard links",
                    ),
                ):
                    verify_artifacts.discover_bundle_metadata_files(artifacts_root)
                metadata_path.unlink()

                asset_path = artifacts_root / "package.zip"
                os.link(source, asset_path)
                with (
                    mock.patch.object(
                        pathlib.Path,
                        "open",
                        side_effect=AssertionError("hard-linked asset was read"),
                    ),
                    self.assertRaisesRegex(
                        verify_artifacts.ArtifactLayoutError,
                        "hard links",
                    ),
                ):
                    verify_artifacts.require_bundle_asset(
                        artifacts_root,
                        artifacts_root,
                        asset_path.name,
                    )
                asset_path.unlink()

    def test_artifact_layout_rejects_symbolic_links_and_root_aliases(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            source = root / "source.json"
            source.write_text("not read", encoding="utf-8")
            link_path = artifacts_root / "metadata-linux-x86_64.json"
            try:
                link_path.symlink_to(source)
            except (NotImplementedError, OSError):
                self.skipTest("symbolic-link creation is unavailable")
            with (
                mock.patch.object(
                    pathlib.Path,
                    "open",
                    side_effect=AssertionError("linked metadata was read"),
                ),
                self.assertRaisesRegex(
                    verify_artifacts.ArtifactLayoutError,
                    "link or reparse point",
                ),
            ):
                verify_artifacts.discover_bundle_metadata_files(artifacts_root)

            link_path.unlink()
            asset_link = artifacts_root / "package.zip"
            asset_link.symlink_to(source)
            with (
                mock.patch.object(
                    pathlib.Path,
                    "open",
                    side_effect=AssertionError("linked asset was read"),
                ),
                self.assertRaisesRegex(
                    verify_artifacts.ArtifactLayoutError,
                    "link or reparse point",
                ),
            ):
                verify_artifacts.require_bundle_asset(
                    artifacts_root,
                    artifacts_root,
                    asset_link.name,
                )

            asset_link.unlink()
            artifact_root_alias = root / "artifacts-alias"
            try:
                artifact_root_alias.symlink_to(artifacts_root, target_is_directory=True)
            except OSError:
                self.skipTest("directory symbolic-link creation is unavailable")
            with self.assertRaisesRegex(
                verify_artifacts.ArtifactLayoutError,
                "artifacts root.*link or reparse point",
            ):
                verify_artifacts.discover_bundle_metadata_files(artifact_root_alias)

            nested_root = artifacts_root / "nested-root"
            nested_root.mkdir()
            alias_parent = root / "alias-parent"
            alias_parent.symlink_to(artifacts_root, target_is_directory=True)
            with self.assertRaisesRegex(
                verify_artifacts.ArtifactLayoutError,
                "artifacts root.*link or reparse point",
            ):
                verify_artifacts.discover_bundle_metadata_files(
                    alias_parent / nested_root.name
                )

    def test_metadata_and_assets_must_be_at_an_unambiguous_bundle_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            artifacts_root = pathlib.Path(temp)
            bundle_root = artifacts_root / "release-linux"
            nested = bundle_root / "nested"
            other_bundle = artifacts_root / "release-windows"
            nested.mkdir(parents=True)
            other_bundle.mkdir()

            metadata_path = nested / "metadata-linux-x86_64.json"
            metadata_path.write_text("{}", encoding="utf-8")
            failures: list[str] = []
            self.assertEqual(
                {},
                verify_artifacts.discover_metadata_files(artifacts_root, failures),
            )
            self.assertTrue(any("must be at an artifact bundle root" in failure for failure in failures))

            metadata_path.unlink()
            flat_metadata = artifacts_root / "metadata-linux-x86_64.json"
            bundled_metadata = bundle_root / "metadata-windows-x86_64.json"
            flat_metadata.write_text("{}", encoding="utf-8")
            bundled_metadata.write_text("{}", encoding="utf-8")
            failures = []
            self.assertEqual(
                {},
                verify_artifacts.discover_metadata_files(artifacts_root, failures),
            )
            self.assertTrue(any("must use one layout" in failure for failure in failures))
            flat_metadata.unlink()
            bundled_metadata.unlink()

            asset_name = "package.zip"
            (nested / asset_name).write_bytes(b"nested fallback")
            with self.assertRaises(FileNotFoundError):
                verify_artifacts.require_asset(
                    bundle_root,
                    asset_name,
                    artifacts_root=artifacts_root,
                )

            (bundle_root / asset_name).write_bytes(b"selected payload")
            (other_bundle / asset_name.upper()).write_bytes(b"portable collision")
            with self.assertRaisesRegex(
                verify_artifacts.ArtifactLayoutError,
                "not unique",
            ):
                verify_artifacts.require_asset(
                    bundle_root,
                    asset_name,
                    artifacts_root=artifacts_root,
                )

            (nested / asset_name).unlink()
            (other_bundle / asset_name.upper()).unlink()
            self.assertEqual(
                bundle_root / asset_name,
                verify_artifacts.require_asset(
                    bundle_root,
                    asset_name,
                    artifacts_root=artifacts_root,
                ),
            )

            for unsafe_name in ("../outside.zip", "sub/package.zip", "*.zip"):
                with self.subTest(unsafe_name=unsafe_name), self.assertRaises(
                    verify_artifacts.ArtifactLayoutError
                ):
                    verify_artifacts.require_asset(
                        bundle_root,
                        unsafe_name,
                        artifacts_root=artifacts_root,
                    )

    def test_manifest_package_integrity_is_checked_against_the_asset(self) -> None:
        target = verify_artifacts.get_target("windows-x86_64")
        config = target["client"]
        with tempfile.TemporaryDirectory() as temp:
            package_path = pathlib.Path(temp) / config["package_name"]
            package_path.write_bytes(b"package payload")
            manifest = {
                "role": "client",
                "launch_exe": config["launch_exe"],
                "engine_library": config["engine_library"],
                "local_manifest_name": config["local_manifest_name"],
                "package": {
                    "name": config["package_name"],
                    "size": 1,
                    "sha256": hashlib.sha256(b"different payload").hexdigest(),
                },
                "files": [],
            }
            failures: list[str] = []

            verify_artifacts.validate_manifest(
                failures,
                target,
                "client",
                manifest,
                manifest_name=config["manifest_name"],
                package_name=config["package_name"],
                required_paths=[],
                forbidden_paths=[],
                package_path=package_path,
            )

        self.assertTrue(any("package size mismatch" in failure for failure in failures))
        self.assertTrue(any("package sha256 mismatch" in failure for failure in failures))

    def test_manifest_loader_enforces_the_serialized_updater_limit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            (root / "payload.json").write_bytes(b"{}")

            with mock.patch.object(verify_artifacts, "MAX_UPDATE_MANIFEST_BYTES", 2):
                self.assertEqual({}, verify_artifacts.load_manifest(root, "payload.json"))
            with (
                mock.patch.object(verify_artifacts, "MAX_UPDATE_MANIFEST_BYTES", 1),
                self.assertRaisesRegex(ValueError, "updater supports at most"),
            ):
                verify_artifacts.load_manifest(root, "payload.json")

    def test_manifest_rejects_managed_count_and_journal_overflow(self) -> None:
        target = verify_artifacts.get_target("windows-x86_64")
        config = target["client"]
        manifest = {
            "role": "client",
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "local_manifest_name": config["local_manifest_name"],
            "package": {
                "name": config["package_name"],
                "size": 1,
                "sha256": "a" * 64,
            },
            "files": [
                {"path": "basew/member.dat", "size": 1, "sha256": "b" * 64}
            ],
        }

        failures: list[str] = []
        with mock.patch.object(verify_artifacts, "MAX_MANAGED_FILES", 0):
            verify_artifacts.validate_manifest(
                failures,
                target,
                "client",
                manifest,
                manifest_name=config["manifest_name"],
                package_name=config["package_name"],
                required_paths=[],
                forbidden_paths=[],
            )
        self.assertTrue(any("managed files" in failure for failure in failures))

        failures = []
        journal_size = verify_artifacts.estimate_install_journal_size(
            ["basew/member.dat"],
            config["local_manifest_name"],
        )
        with mock.patch.object(
            verify_artifacts,
            "MAX_INSTALL_JOURNAL_BYTES",
            journal_size - 1,
        ):
            verify_artifacts.validate_manifest(
                failures,
                target,
                "client",
                manifest,
                manifest_name=config["manifest_name"],
                package_name=config["package_name"],
                required_paths=[],
                forbidden_paths=[],
            )
        self.assertTrue(any("install journal" in failure for failure in failures))

    def test_package_limit_is_checked_before_hashing_or_opening_the_archive(self) -> None:
        target = verify_artifacts.get_target("windows-x86_64")
        config = target["client"]
        with tempfile.TemporaryDirectory() as temp:
            package_path = pathlib.Path(temp) / config["package_name"]
            package_path.write_bytes(b"xx")
            manifest = {
                "role": "client",
                "launch_exe": config["launch_exe"],
                "engine_library": config["engine_library"],
                "local_manifest_name": config["local_manifest_name"],
                "package": {
                    "name": config["package_name"],
                    "size": 2,
                    "sha256": hashlib.sha256(b"xx").hexdigest(),
                },
                "files": [],
            }
            failures: list[str] = []
            with (
                mock.patch.object(verify_artifacts, "MAX_UPDATE_PACKAGE_BYTES", 1),
                mock.patch.object(verify_artifacts, "sha256_file") as sha256_file,
            ):
                verify_artifacts.validate_manifest(
                    failures,
                    target,
                    "client",
                    manifest,
                    manifest_name=config["manifest_name"],
                    package_name=config["package_name"],
                    required_paths=[],
                    forbidden_paths=[],
                    package_path=package_path,
                    package_format="zip",
                )

            sha256_file.assert_not_called()
            self.assertTrue(any("declares a 2-byte package" in failure for failure in failures))
            self.assertTrue(any("package asset is 2 bytes" in failure for failure in failures))
            self.assertFalse(any("not a valid zip archive" in failure for failure in failures))

    def test_manifest_release_identity_must_match_platform_metadata(self) -> None:
        target = verify_artifacts.get_target("windows-x86_64")
        config = target["client"]
        metadata = self.canonical_metadata(target)
        manifest = {
            "schema_version": 2,
            "repo": metadata["repo"],
            "channel": metadata["channel"],
            "version": "9.9.9",
            "role": "client",
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "local_manifest_name": config["local_manifest_name"],
            "platform": {"id": "wrong", "os": target["os"], "arch": target["arch"]},
            "build": {"id": metadata["build_id"], "commit_sha": "b" * 40},
            "package": {
                "name": config["package_name"],
                "size": 1,
                "sha256": "a" * 64,
            },
            "files": [],
        }
        failures: list[str] = []

        verify_artifacts.validate_manifest(
            failures,
            target,
            "client",
            manifest,
            manifest_name=config["manifest_name"],
            package_name=config["package_name"],
            required_paths=[],
            forbidden_paths=[],
            release_metadata=metadata,
        )

        self.assertTrue(any("schema_version mismatch" in failure for failure in failures))
        self.assertTrue(any("version mismatch" in failure for failure in failures))
        self.assertTrue(any("platform contract mismatch" in failure for failure in failures))
        self.assertTrue(any("build identity mismatch" in failure for failure in failures))

    def test_zip_and_tar_members_must_match_manifest_hashes(self) -> None:
        payload = b"member payload"
        files = [
            {
                "path": "basew/member.dat",
                "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        ]
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            source = root / "member.dat"
            source.write_bytes(payload)
            local_manifest = json.dumps({"install_state": True}).encode("utf-8")
            zip_path = root / "payload.zip"
            with zipfile.ZipFile(zip_path, "w") as archive:
                archive.write(source, "basew/member.dat")
                archive.writestr("worr_install_manifest.json", local_manifest)
            tar_path = root / "payload.tar.gz"
            local_manifest_path = root / "worr_install_manifest.json"
            local_manifest_path.write_bytes(local_manifest)
            with tarfile.open(tar_path, "w:gz") as archive:
                archive.add(source, arcname="basew/member.dat", recursive=False)
                archive.add(
                    local_manifest_path,
                    arcname="worr_install_manifest.json",
                    recursive=False,
                )

            for archive_path, archive_format in ((zip_path, "zip"), (tar_path, "tar.gz")):
                with self.subTest(archive_format=archive_format):
                    failures: list[str] = []
                    verify_artifacts.validate_archive_members(
                        failures,
                        platform_id="test-x86_64",
                        role="client",
                        manifest_name="payload.json",
                        package_path=archive_path,
                        archive_format=archive_format,
                        files=files,
                        local_manifest_name="worr_install_manifest.json",
                    )
                    self.assertEqual([], failures)

    def test_packaged_updater_config_must_embed_the_signer_policy(self) -> None:
        expected_policy = {
            "schema_version": 1,
            "threshold": 1,
            "keys": [{"key_id": "a" * 64}],
        }
        cases = (
            ({"release_trust_policy": expected_policy}, False),
            ({"release_trust_policy": {**expected_policy, "threshold": 2}}, True),
            (
                {
                    "release_trust_policy": expected_policy,
                    "allow_unsigned_development_index": True,
                },
                True,
            ),
        )
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            for index, (config, should_fail) in enumerate(cases):
                with self.subTest(config=config):
                    config_bytes = (json.dumps(config) + "\n").encode("utf-8")
                    local_manifest = json.dumps({"install_state": True}).encode("utf-8")
                    files = [
                        {
                            "path": "worr_update.json",
                            "size": len(config_bytes),
                            "sha256": hashlib.sha256(config_bytes).hexdigest(),
                        }
                    ]
                    package_path = root / f"policy-{index}.zip"
                    with zipfile.ZipFile(package_path, "w") as archive:
                        archive.writestr("worr_update.json", config_bytes)
                        archive.writestr(
                            "worr_install_manifest.json", local_manifest
                        )
                    failures: list[str] = []
                    verify_artifacts.validate_archive_members(
                        failures,
                        platform_id="test-x86_64",
                        role="client",
                        manifest_name="payload.json",
                        package_path=package_path,
                        archive_format="zip",
                        files=files,
                        local_manifest_name="worr_install_manifest.json",
                        expected_trust_policy=expected_policy,
                    )
                    self.assertEqual(should_fail, bool(failures), failures)
                    if should_fail:
                        self.assertTrue(
                            any(
                                "trust policy differs" in failure
                                or "unsigned development" in failure
                                for failure in failures
                            )
                        )

    def test_production_updater_config_must_exactly_match_release_contract(self) -> None:
        policy = {
            "schema_version": 1,
            "threshold": 1,
            "keys": [{"key_id": "a" * 64}],
        }
        base_config = {
            "repo": "owner/worr",
            "channel": "stable",
            "role": "client",
            "release_index_asset": "worr-release-index-stable.json",
            "launch_exe": "worr.exe",
            "autolaunch": True,
            "allow_prerelease": False,
            "preserve": list(verify_artifacts.DEFAULT_PRESERVE),
            "release_trust_policy": policy,
        }
        cases = (
            ("canonical", base_config, False),
            ("unexpected key", {**base_config, "unreviewed": True}, True),
            ("missing key", {key: value for key, value in base_config.items() if key != "autolaunch"}, True),
            ("autolaunch disabled", {**base_config, "autolaunch": False}, True),
            ("prerelease enabled", {**base_config, "allow_prerelease": True}, True),
            ("preserve changed", {**base_config, "preserve": ["basew/*.cfg"]}, True),
        )

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            for index, (label, config, should_fail) in enumerate(cases):
                with self.subTest(case=label):
                    config_bytes = json.dumps(config, sort_keys=True).encode("utf-8")
                    files = [
                        {
                            "path": "worr_update.json",
                            "size": len(config_bytes),
                            "sha256": hashlib.sha256(config_bytes).hexdigest(),
                        }
                    ]
                    external_manifest = {
                        "schema_version": 3,
                        "repo": "owner/worr",
                        "channel": "stable",
                        "role": "client",
                        "launch_exe": "worr.exe",
                        "files": files,
                    }
                    local_manifest = dict(external_manifest)
                    local_manifest["install_state"] = True
                    package_path = root / f"config-{index}.zip"
                    with zipfile.ZipFile(package_path, "w") as archive:
                        archive.writestr("worr_update.json", config_bytes)
                        archive.writestr(
                            "worr_install_manifest.json",
                            json.dumps(local_manifest).encode("utf-8"),
                        )

                    failures: list[str] = []
                    verify_artifacts.validate_archive_members(
                        failures,
                        platform_id="windows-x86_64",
                        role="client",
                        manifest_name="worr-client-update.json",
                        package_path=package_path,
                        archive_format="zip",
                        files=files,
                        local_manifest_name="worr_install_manifest.json",
                        external_manifest=external_manifest,
                        expected_trust_policy=policy,
                        release_metadata={
                            "repo": "owner/worr",
                            "channel": "stable",
                        },
                    )
                    self.assertEqual(should_fail, bool(failures), failures)

    def test_nightly_updater_config_requires_prerelease_channel(self) -> None:
        policy = {
            "schema_version": 1,
            "threshold": 1,
            "keys": [{"key_id": "a" * 64}],
        }
        config = {
            "repo": "owner/worr",
            "channel": "nightly",
            "role": "client",
            "release_index_asset": "worr-release-index-nightly.json",
            "launch_exe": "worr.exe",
            "autolaunch": True,
            "allow_prerelease": True,
            "preserve": list(verify_artifacts.DEFAULT_PRESERVE),
            "release_trust_policy": policy,
        }
        config_bytes = json.dumps(config, sort_keys=True).encode("utf-8")
        files = [
            {
                "path": "worr_update.json",
                "size": len(config_bytes),
                "sha256": hashlib.sha256(config_bytes).hexdigest(),
            }
        ]
        external_manifest = {
            "schema_version": 3,
            "repo": "owner/worr",
            "channel": "nightly",
            "role": "client",
            "launch_exe": "worr.exe",
            "files": files,
        }
        local_manifest = {**external_manifest, "install_state": True}

        with tempfile.TemporaryDirectory() as temp:
            package_path = pathlib.Path(temp) / "nightly.zip"
            with zipfile.ZipFile(package_path, "w") as archive:
                archive.writestr("worr_update.json", config_bytes)
                archive.writestr(
                    "worr_install_manifest.json",
                    json.dumps(local_manifest).encode("utf-8"),
                )
            failures: list[str] = []
            verify_artifacts.validate_archive_members(
                failures,
                platform_id="windows-x86_64",
                role="client",
                manifest_name="worr-client-update.json",
                package_path=package_path,
                archive_format="zip",
                files=files,
                local_manifest_name="worr_install_manifest.json",
                external_manifest=external_manifest,
                expected_trust_policy=policy,
                release_metadata={"repo": "owner/worr", "channel": "nightly"},
            )

        self.assertEqual([], failures)

    def test_local_manifest_cannot_bypass_semantic_validation_as_a_managed_file(self) -> None:
        local_manifest_name = "worr_install_manifest.json"
        bogus_local_manifest = json.dumps(
            {"install_state": True, "version": "999.0.0"}
        ).encode("utf-8")
        files = [
            {
                "path": local_manifest_name,
                "size": len(bogus_local_manifest),
                "sha256": hashlib.sha256(bogus_local_manifest).hexdigest(),
            }
        ]
        external_manifest = {
            "schema_version": 3,
            "version": "1.0.0",
            "files": files,
        }

        with tempfile.TemporaryDirectory() as temp:
            package_path = pathlib.Path(temp) / "payload.zip"
            with zipfile.ZipFile(package_path, "w") as archive:
                archive.writestr(local_manifest_name, bogus_local_manifest)

            failures: list[str] = []
            verify_artifacts.validate_archive_members(
                failures,
                platform_id="windows-x86_64",
                role="client",
                manifest_name="payload.json",
                package_path=package_path,
                archive_format="zip",
                files=files,
                local_manifest_name=local_manifest_name,
                external_manifest=external_manifest,
            )

        self.assertTrue(any("must not include local install manifest" in failure for failure in failures))
        self.assertTrue(any("local install manifest does not match" in failure for failure in failures))

    def test_zip_non_ascii_names_require_the_utf8_flag(self) -> None:
        payload = b"x"
        files = [
            {
                "path": "café.dat",
                "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        ]
        external_manifest = {
            "schema_version": 3,
            "version": "1.0.0",
            "files": files,
        }
        local_manifest = dict(external_manifest)
        local_manifest["install_state"] = True

        with tempfile.TemporaryDirectory() as temp:
            package_path = pathlib.Path(temp) / "payload.zip"
            with zipfile.ZipFile(package_path, "w", compression=zipfile.ZIP_STORED) as archive:
                archive.writestr("cafe.dat", payload)
                archive.writestr(
                    "worr_install_manifest.json",
                    json.dumps(local_manifest).encode("utf-8"),
                )
            package_path.write_bytes(
                package_path.read_bytes().replace(b"cafe.dat", b"caf\x82.dat")
            )

            failures: list[str] = []
            verify_artifacts.validate_archive_members(
                failures,
                platform_id="windows-x86_64",
                role="client",
                manifest_name="payload.json",
                package_path=package_path,
                archive_format="zip",
                files=files,
                local_manifest_name="worr_install_manifest.json",
                external_manifest=external_manifest,
            )

        self.assertTrue(any("without the UTF-8 flag" in failure for failure in failures))

    def test_non_archive_and_unexpected_archive_members_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            invalid_path = root / "invalid.zip"
            invalid_path.write_bytes(b"not a zip")
            failures: list[str] = []
            verify_artifacts.validate_archive_members(
                failures,
                platform_id="test-x86_64",
                role="client",
                manifest_name="payload.json",
                package_path=invalid_path,
                archive_format="zip",
                files=[],
                local_manifest_name="worr_install_manifest.json",
            )
            self.assertTrue(any("not a valid zip archive" in failure for failure in failures))

            extra_path = root / "extra.zip"
            with zipfile.ZipFile(extra_path, "w") as archive:
                archive.writestr("unexpected.txt", b"unexpected")
            failures = []
            verify_artifacts.validate_archive_members(
                failures,
                platform_id="test-x86_64",
                role="client",
                manifest_name="payload.json",
                package_path=extra_path,
                archive_format="zip",
                files=[],
                local_manifest_name="worr_install_manifest.json",
            )
            self.assertTrue(any("unexpected archive member 'unexpected.txt'" in failure for failure in failures))

    def test_zip_unix_special_file_types_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            package_path = pathlib.Path(temp) / "special.zip"
            special = zipfile.ZipInfo("special.dat")
            special.create_system = 3
            special.external_attr = 0o010644 << 16
            with zipfile.ZipFile(package_path, "w") as archive:
                archive.writestr(special, b"special")

            failures: list[str] = []
            verify_artifacts.validate_archive_members(
                failures,
                platform_id="linux-x86_64",
                role="client",
                manifest_name="payload.json",
                package_path=package_path,
                archive_format="zip",
                files=[],
                local_manifest_name="worr_install_manifest.json",
            )

        self.assertTrue(any("non-regular archive member 'special.dat'" in failure for failure in failures))

    def test_archive_member_case_must_exactly_match_the_manifest(self) -> None:
        payload = b"payload"
        files = [
            {
                "path": "basew/safe.dat",
                "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        ]
        with tempfile.TemporaryDirectory() as temp:
            package_path = pathlib.Path(temp) / "payload.zip"
            with zipfile.ZipFile(package_path, "w") as archive:
                archive.writestr("BASEW/SAFE.DAT", payload)
            failures: list[str] = []

            verify_artifacts.validate_archive_members(
                failures,
                platform_id="linux-x86_64",
                role="client",
                manifest_name="payload.json",
                package_path=package_path,
                archive_format="zip",
                files=files,
                local_manifest_name="worr_install_manifest.json",
            )

        self.assertTrue(any("does not exactly match 'basew/safe.dat'" in failure for failure in failures))

    def test_installer_requires_structural_and_semantic_msi_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            installer_path = pathlib.Path(temp) / "worr-win64.msi"
            invalid_directory = bytearray(minimal_compound_file())
            struct.pack_into("<I", invalid_directory, 48, 99)
            invalid_payloads = (
                b"not an msi",
                verify_artifacts.MSI_COMPOUND_FILE_SIGNATURE + bytes(504),
                minimal_compound_file(bytes(16)),
                bytes(invalid_directory),
            )
            for payload in invalid_payloads:
                with self.subTest(size=len(payload), root=payload[-432:-416].hex()):
                    installer_path.write_bytes(payload)
                    failures: list[str] = []
                    verify_artifacts.validate_installer_asset(
                        failures,
                        "windows-x86_64",
                        installer_path,
                        "msi",
                    )
                    self.assertTrue(any("is not a structurally valid MSI" in failure for failure in failures))

            installer_path.write_bytes(minimal_compound_file())
            failures = []
            verify_artifacts.validate_installer_asset(
                failures,
                "windows-x86_64",
                installer_path,
                "msi",
            )
            self.assertTrue(
                any("is not a usable MSI database" in failure for failure in failures)
            )

            with mock.patch.object(verify_artifacts, "validate_msi_database"):
                failures = []
                verify_artifacts.validate_installer_asset(
                    failures,
                    "windows-x86_64",
                    installer_path,
                    "msi",
                )
            self.assertEqual([], failures)

    def test_msiinfo_validation_requires_tool_file_table_and_payload_row(self) -> None:
        installer_path = pathlib.Path("release.msi")
        valid_export = (
            "File\tComponent_\tFileName\tFileSize\n"
            "s72\ts72\tl255\ti4\n"
            "File\tFile\n"
            "WorrExe\tMainComponent\tworr.exe\t42\n"
        )

        with (
            mock.patch.object(verify_artifacts.shutil, "which", return_value=None),
            self.assertRaisesRegex(ValueError, "msiinfo from msitools is required"),
        ):
            verify_artifacts._validate_msi_with_msiinfo(installer_path)

        cases = (
            (("Property\n", valid_export), "does not contain a File table"),
            (
                (
                    "File\n",
                    "File\tComponent_\tFileName\tFileSize\n"
                    "s72\ts72\tl255\ti4\n"
                    "File\tFile\n",
                ),
                "contains no payload rows",
            ),
            (
                (
                    "File\n",
                    "File\tComponent_\tFileName\n"
                    "s72\ts72\tl255\n"
                    "File\tFile\n"
                    "WorrExe\tMainComponent\tworr.exe\n",
                ),
                "invalid File table schema",
            ),
        )
        for outputs, expected_error in cases:
            with (
                self.subTest(expected_error=expected_error),
                mock.patch.object(
                    verify_artifacts.shutil,
                    "which",
                    return_value="/usr/bin/msiinfo",
                ),
                mock.patch.object(
                    verify_artifacts,
                    "_run_msiinfo",
                    side_effect=outputs,
                ),
                self.assertRaisesRegex(ValueError, expected_error),
            ):
                verify_artifacts._validate_msi_with_msiinfo(installer_path)

        with (
            mock.patch.object(
                verify_artifacts.shutil,
                "which",
                return_value="/usr/bin/msiinfo",
            ),
            mock.patch.object(
                verify_artifacts,
                "_run_msiinfo",
                side_effect=("Property\nFile\nMedia\n", valid_export),
            ) as run_msiinfo,
        ):
            verify_artifacts._validate_msi_with_msiinfo(installer_path)
        self.assertEqual(2, run_msiinfo.call_count)

    def test_msi_payload_is_bound_to_the_exact_client_manifest_tree(self) -> None:
        manifest, payloads = msi_payload_fixture()
        prefix = "Program Files/WORR"
        listing = "".join(f"{prefix}/{path}\n" for path in payloads)

        def fake_msiextract(_tool: str, *arguments: str) -> str:
            if arguments[0] == "--list":
                return listing
            self.assertEqual("--directory", arguments[0])
            extraction_root = pathlib.Path(arguments[1])
            for path, payload in payloads.items():
                output = extraction_root / prefix / pathlib.PurePosixPath(path)
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(payload)
            return listing

        with (
            mock.patch.object(
                verify_artifacts.shutil,
                "which",
                return_value="/usr/bin/msiextract",
            ),
            mock.patch.object(
                verify_artifacts,
                "_run_msiextract",
                side_effect=fake_msiextract,
            ) as run_msiextract,
        ):
            verify_artifacts._validate_msi_payload_against_manifest(
                pathlib.Path("worr-win64.msi"),
                manifest,
                "worr_install_manifest.json",
            )

        self.assertEqual(2, run_msiextract.call_count)

    def test_msi_payload_verification_fails_closed_before_extraction(self) -> None:
        manifest, payloads = msi_payload_fixture()
        installer_path = pathlib.Path("worr-win64.msi")

        with (
            mock.patch.object(verify_artifacts.shutil, "which", return_value=None),
            self.assertRaisesRegex(ValueError, "msiextract from msitools is required"),
        ):
            verify_artifacts._validate_msi_payload_against_manifest(
                installer_path,
                manifest,
                "worr_install_manifest.json",
            )

        unsafe_listings = (
            ("../escape.exe\n", "Invalid MSI payload path"),
            (
                "Program Files/WORR/worr.exe\n"
                "Program Files/WORR/WORR.EXE\n",
                "collides with",
            ),
            (
                "Program Files/WORR/worr.exe\n"
                "Program Files/WORR/worr.exe/child\n",
                "prefix collision",
            ),
        )
        for listing, expected_error in unsafe_listings:
            with (
                self.subTest(expected_error=expected_error),
                mock.patch.object(
                    verify_artifacts.shutil,
                    "which",
                    return_value="/usr/bin/msiextract",
                ),
                mock.patch.object(
                    verify_artifacts,
                    "_run_msiextract",
                    return_value=listing,
                ) as run_msiextract,
                self.assertRaisesRegex(ValueError, expected_error),
            ):
                verify_artifacts._validate_msi_payload_against_manifest(
                    installer_path,
                    manifest,
                    "worr_install_manifest.json",
                )
            run_msiextract.assert_called_once()

        incomplete_listing = "".join(
            f"Program Files/WORR/{path}\n"
            for path in payloads
            if path != "worr.exe"
        )
        with (
            mock.patch.object(
                verify_artifacts.shutil,
                "which",
                return_value="/usr/bin/msiextract",
            ),
            mock.patch.object(
                verify_artifacts,
                "_run_msiextract",
                return_value=incomplete_listing,
            ) as run_msiextract,
            self.assertRaisesRegex(ValueError, "does not describe exactly one install root"),
        ):
            verify_artifacts._validate_msi_payload_against_manifest(
                installer_path,
                manifest,
                "worr_install_manifest.json",
            )
        run_msiextract.assert_called_once()

    def test_msi_extraction_rejects_missing_extra_and_modified_files(self) -> None:
        manifest, payloads = msi_payload_fixture()
        prefix = "Program Files/WORR"
        listing = "".join(f"{prefix}/{path}\n" for path in payloads)
        installer_path = pathlib.Path("worr-win64.msi")

        def run_case(case: str) -> None:
            def fake_msiextract(_tool: str, *arguments: str) -> str:
                if arguments[0] == "--list":
                    return listing
                extraction_root = pathlib.Path(arguments[1])
                extracted_payloads = dict(payloads)
                if case == "missing":
                    extracted_payloads.pop("worr.exe")
                elif case == "modified":
                    extracted_payloads["worr.exe"] = b"malicious bytes!"
                elif case == "local-manifest":
                    local_manifest = json.loads(
                        extracted_payloads["worr_install_manifest.json"]
                    )
                    local_manifest["repo"] = "attacker/WORR"
                    extracted_payloads["worr_install_manifest.json"] = json.dumps(
                        local_manifest,
                        indent=2,
                    ).encode("utf-8")
                for path, payload in extracted_payloads.items():
                    output = extraction_root / prefix / pathlib.PurePosixPath(path)
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_bytes(payload)
                if case == "extra":
                    extra = extraction_root / prefix / "unexpected.dll"
                    extra.write_bytes(b"extra")
                return listing

            with (
                mock.patch.object(
                    verify_artifacts.shutil,
                    "which",
                    return_value="/usr/bin/msiextract",
                ),
                mock.patch.object(
                    verify_artifacts,
                    "_run_msiextract",
                    side_effect=fake_msiextract,
                ),
            ):
                verify_artifacts._validate_msi_payload_against_manifest(
                    installer_path,
                    manifest,
                    "worr_install_manifest.json",
                )

        cases = (
            ("missing", "file set differs.*missing"),
            ("extra", "file set differs.*unexpected"),
            ("modified", "sha256 mismatch"),
            ("local-manifest", "local install manifest does not match"),
        )
        for case, expected_error in cases:
            with self.subTest(case=case), self.assertRaisesRegex(ValueError, expected_error):
                run_case(case)

    def test_extracted_msi_tree_rejects_links_special_files_and_case_collisions(self) -> None:
        class FakeEntry:
            def __init__(self, name: str, file_mode: int) -> None:
                self.name = name
                self.path = name
                self._file_mode = file_mode

            def stat(self, *, follow_symlinks: bool) -> object:
                self_following = follow_symlinks
                if self_following:
                    raise AssertionError("scanner followed an extracted MSI link")
                return type("Status", (), {"st_mode": self._file_mode})()

        cases = (
            ([FakeEntry("payload", stat.S_IFLNK)], "symbolic link"),
            ([FakeEntry("payload", stat.S_IFIFO)], "not a regular file"),
            (
                [
                    FakeEntry("payload.dll", stat.S_IFREG),
                    FakeEntry("PAYLOAD.DLL", stat.S_IFREG),
                ],
                "case collision",
            ),
        )
        for entries, expected_error in cases:
            with (
                self.subTest(expected_error=expected_error),
                mock.patch.object(verify_artifacts.os, "scandir", return_value=entries),
                self.assertRaisesRegex(ValueError, expected_error),
            ):
                verify_artifacts._scan_extracted_msi_tree(pathlib.Path("unused"))

    def test_manifest_rejects_unsafe_and_case_colliding_file_paths(self) -> None:
        target = verify_artifacts.get_target("windows-x86_64")
        config = target["client"]
        manifest = {
            "role": "client",
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "local_manifest_name": config["local_manifest_name"],
            "package": {"name": config["package_name"], "size": 1, "sha256": "a" * 64},
            "files": [
                {"path": "../escape.dll", "size": 1, "sha256": "b" * 64},
                {"path": "basew/safe.dat", "size": 1, "sha256": "c" * 64},
                {"path": "BASEW/SAFE.DAT", "size": 1, "sha256": "d" * 64},
                {"path": "basew/data", "size": 1, "sha256": "e" * 64},
                {"path": "BASEW/DATA/child.dat", "size": 1, "sha256": "f" * 64},
            ],
        }
        failures: list[str] = []

        verify_artifacts.validate_manifest(
            failures,
            target,
            "client",
            manifest,
            manifest_name=config["manifest_name"],
            package_name=config["package_name"],
            required_paths=[],
            forbidden_paths=[],
        )

        self.assertTrue(any("unsafe file path '../escape.dll'" in failure for failure in failures))
        self.assertTrue(any("duplicate file path 'BASEW/SAFE.DAT'" in failure for failure in failures))
        self.assertTrue(any("file/directory prefix collision" in failure for failure in failures))

    def test_manifest_prefix_collision_uses_a_component_boundary(self) -> None:
        target = verify_artifacts.get_target("windows-x86_64")
        config = target["client"]
        manifest = {
            "role": "client",
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "local_manifest_name": config["local_manifest_name"],
            "package": {
                "name": config["package_name"],
                "size": 1,
                "sha256": "a" * 64,
            },
            "files": [
                {
                    "path": "basew/config",
                    "size": 1,
                    "sha256": "b" * 64,
                    "mode": 0o644,
                },
                {
                    "path": "BASEW/CONFIG-old/settings.cfg",
                    "size": 1,
                    "sha256": "c" * 64,
                    "mode": 0o644,
                },
            ],
        }
        failures: list[str] = []

        verify_artifacts.validate_manifest(
            failures,
            target,
            "client",
            manifest,
            manifest_name=config["manifest_name"],
            package_name=config["package_name"],
            required_paths=[],
            forbidden_paths=[],
        )

        self.assertFalse(any("file/directory prefix collision" in failure for failure in failures))

    def test_empty_metadata_cannot_pass_as_a_complete_release(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            artifacts_root = pathlib.Path(temp)
            metadata = {
                "platform_id": "windows-x86_64",
                "os": "windows",
                "arch": "x86_64",
                "artifacts": [],
            }
            (artifacts_root / "metadata-windows-x86_64.json").write_text(
                json.dumps(metadata), encoding="utf-8"
            )

            stdout = io.StringIO()
            argv = [
                "verify_artifacts.py",
                "--artifacts-root",
                str(artifacts_root),
                "--platform-id",
                "windows-x86_64",
            ]
            with mock.patch.object(sys, "argv", argv), contextlib.redirect_stdout(stdout):
                result = verify_artifacts.main()

            self.assertEqual(1, result)
            output = stdout.getvalue()
            self.assertIn("metadata missing required artifact worr-client-win64.zip", output)
            self.assertIn("missing worr-client-win64.json", output)

    def test_metadata_identity_must_match_the_registry_target(self) -> None:
        target = verify_artifacts.get_target("linux-x86_64")
        failures: list[str] = []
        metadata = {
            "platform_id": target["platform_id"],
            "os": "windows",
            "arch": target["arch"],
            "artifacts": [],
        }

        with tempfile.TemporaryDirectory() as temp:
            verify_artifacts.validate_target_bundle(pathlib.Path(temp), target, metadata, failures)

        self.assertIn("linux-x86_64: metadata os mismatch ('windows' != 'linux')", failures)


if __name__ == "__main__":
    unittest.main()
