from __future__ import annotations

import copy
import io
import pathlib
import shutil
import stat
import tempfile
import unittest
import zipfile

from tools.dependencies import verify_dependency_baseline as verifier


class DependencyBaselineTests(unittest.TestCase):
    def copy_contract_repository(self, destination: pathlib.Path) -> dict:
        baseline = verifier.load_baseline()
        destination.mkdir(parents=True, exist_ok=True)
        source_subprojects = verifier.REPO_ROOT / "subprojects"
        target_subprojects = destination / "subprojects"
        target_subprojects.mkdir()

        for name in baseline["wraps"]:
            shutil.copy2(source_subprojects / name, target_subprojects / name)
        shutil.copy2(source_subprojects / "wraps.zip", target_subprojects / "wraps.zip")
        shutil.copytree(
            source_subprojects / "packagefiles",
            target_subprojects / "packagefiles",
        )

        meson_files = set(baseline["meson"]["root_files"])
        meson_files.update(baseline["meson"]["consumer_files"])
        for filename in sorted(meson_files):
            source = verifier.REPO_ROOT / filename
            target = destination / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        return baseline

    def test_repository_dependency_baseline_is_consistent(self) -> None:
        baseline = verifier.load_baseline()
        self.assertEqual(19, len(baseline["wraps"]))
        self.assertIn("monocypher.wrap", baseline["wraps"])
        self.assertIn("rmlui.wrap", baseline["wraps"])

        result = verifier.verify_repository()

        self.assertEqual([], result.errors)

    def test_wrap_archive_is_byte_reproducible_and_canonical(self) -> None:
        baseline = verifier.load_baseline()
        first = verifier.build_wrap_archive_bytes(verifier.REPO_ROOT, baseline)
        second = verifier.build_wrap_archive_bytes(verifier.REPO_ROOT, baseline)
        self.assertEqual(first, second)
        self.assertEqual(
            (verifier.REPO_ROOT / baseline["archive"]["path"]).read_bytes(),
            first,
        )

        with zipfile.ZipFile(io.BytesIO(first)) as archive:
            settings = verifier.archive_settings(baseline)
            self.assertEqual(sorted(baseline["wraps"]), archive.namelist())
            for info in archive.infolist():
                with self.subTest(name=info.filename):
                    self.assertEqual(settings.timestamp, info.date_time)
                    self.assertEqual(settings.compression, info.compress_type)
                    self.assertEqual(3, info.create_system)
                    self.assertEqual(
                        settings.entry_mode,
                        stat.S_IMODE(info.external_attr >> 16),
                    )
                    self.assertEqual(
                        (verifier.REPO_ROOT / "subprojects" / info.filename).read_bytes(),
                        archive.read(info),
                    )

    def test_archive_contract_and_wrap_names_fail_closed(self) -> None:
        baseline = verifier.load_baseline()
        bad_contract = copy.deepcopy(baseline)
        bad_contract["archive"]["timestamp"] = "1981-01-01T00:00:00"
        with self.assertRaisesRegex(ValueError, "archive timestamp"):
            verifier.build_wrap_archive_bytes(verifier.REPO_ROOT, bad_contract)

        unsafe_wrap = copy.deepcopy(baseline)
        descriptor = unsafe_wrap["wraps"].pop("fmt.wrap")
        unsafe_wrap["wraps"]["../fmt.wrap"] = descriptor
        with self.assertRaisesRegex(ValueError, "plain .wrap filename"):
            verifier.build_wrap_archive_bytes(verifier.REPO_ROOT, unsafe_wrap)

    def test_wrap_descriptor_scope_version_and_fields_are_validated(self) -> None:
        baseline = verifier.load_baseline()
        mutations = (
            ("scope", "optional", "invalid scope"),
            ("version", "", "non-empty version"),
            ("unexpected", "field", "must contain exactly"),
        )
        for field, value, expected_error in mutations:
            with self.subTest(field=field):
                mutated = copy.deepcopy(baseline)
                mutated["wraps"]["fmt.wrap"][field] = value

                result = verifier.verify_wrap_inventory(verifier.REPO_ROOT, mutated)

                self.assertTrue(
                    any(expected_error in error for error in result.errors),
                    result.errors,
                )

    def test_verifier_rejects_wrap_archive_and_local_source_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo_root = pathlib.Path(temp)
            baseline = self.copy_contract_repository(repo_root)
            baseline_path = verifier.DEFAULT_BASELINE

            wrap_path = repo_root / "subprojects" / "fmt.wrap"
            wrap_path.write_text(
                wrap_path.read_text(encoding="utf-8").replace(
                    "directory = fmt-12.0.0",
                    "directory = fmt-99.0.0",
                ),
                encoding="utf-8",
            )
            result = verifier.verify_repository(repo_root, baseline_path)
            self.assertTrue(any("fmt.wrap metadata differs" in error for error in result.errors))
            self.assertTrue(any("wraps.zip is not the exact" in error for error in result.errors))

            shutil.copy2(
                verifier.REPO_ROOT / "subprojects" / "fmt.wrap",
                wrap_path,
            )
            local_archive = (
                repo_root / "subprojects" / "packagefiles" / "khr-headers.tar.xz"
            )
            local_archive.write_bytes(local_archive.read_bytes() + b"tamper")
            result = verifier.verify_repository(repo_root, baseline_path)
            self.assertTrue(
                any("khr-headers.tar.xz SHA-256 mismatch" in error for error in result.errors)
            )

            local_archive.write_bytes(
                (
                    verifier.REPO_ROOT
                    / "subprojects"
                    / "packagefiles"
                    / "khr-headers.tar.xz"
                ).read_bytes()
            )
            (repo_root / "subprojects" / "nasm.wrap.disabled").write_text(
                "[wrap-redirect]\nfilename = stale.wrap\n",
                encoding="utf-8",
            )
            result = verifier.verify_repository(repo_root, baseline_path)
            self.assertTrue(any("stale disabled wrap" in error for error in result.errors))
            self.assertEqual(19, len(baseline["wraps"]))

            (repo_root / "subprojects" / "nasm.wrap.disabled").unlink()
            wrap_path.unlink()
            wrap_path.mkdir()
            result = verifier.verify_repository(repo_root, baseline_path)
            self.assertTrue(
                any(
                    "subprojects/fmt.wrap is a non-regular entry" in error
                    for error in result.errors
                )
            )

    def test_verifier_rejects_meson_and_package_override_version_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo_root = pathlib.Path(temp)
            self.copy_contract_repository(repo_root)

            meson_path = repo_root / "meson.build"
            meson_path.write_text(
                meson_path.read_text(encoding="utf-8").replace(
                    "dependency('fmt'",
                    "dependency('fmt-renamed'",
                    1,
                ),
                encoding="utf-8",
            )
            result = verifier.verify_repository(repo_root, verifier.DEFAULT_BASELINE)
            self.assertTrue(
                any("Meson dependency declarations differ" in error for error in result.errors)
            )

            shutil.copy2(verifier.REPO_ROOT / "meson.build", meson_path)
            unexpected_meson = repo_root / "src" / "new" / "meson.build"
            unexpected_meson.parent.mkdir()
            unexpected_meson.write_text(
                "dependency('unexpected-dependency')\n",
                encoding="utf-8",
            )
            result = verifier.verify_repository(repo_root, verifier.DEFAULT_BASELINE)
            self.assertTrue(
                any(
                    "Meson consumer-bearing file inventory differs" in error
                    for error in result.errors
                )
            )
            unexpected_meson.unlink()
            unexpected_meson.parent.rmdir()

            curl_meson = (
                repo_root
                / "subprojects"
                / "packagefiles"
                / "curl"
                / "meson.build"
            )
            curl_meson.write_text(
                curl_meson.read_text(encoding="utf-8").replace(
                    "version: '8.18.0'",
                    "version: '8.17.0'",
                    1,
                ),
                encoding="utf-8",
            )
            result = verifier.verify_repository(repo_root, verifier.DEFAULT_BASELINE)
            self.assertTrue(
                any("packagefiles/curl/meson.build version mismatch" in error for error in result.errors)
            )

    def test_verifier_rejects_packagefiles_root_symlink_when_supported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo_root = pathlib.Path(temp)
            self.copy_contract_repository(repo_root)
            packagefiles = repo_root / "subprojects" / "packagefiles"
            link = packagefiles / "linked-source.tar.xz"
            try:
                link.symlink_to(packagefiles / "khr-headers.tar.xz")
            except (NotImplementedError, OSError):
                self.skipTest("symbolic links are unavailable on this test host")

            result = verifier.verify_repository(repo_root, verifier.DEFAULT_BASELINE)

            self.assertTrue(
                any("packagefiles root entry is a symbolic link" in error for error in result.errors)
            )

    def test_local_tree_audit_reports_stale_duplicate_versions_without_deleting(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            subprojects = pathlib.Path(temp)
            active = subprojects / "widget-2.0.0"
            stale = subprojects / "widget-1.0.0"
            active.mkdir()
            stale.mkdir()

            warnings = verifier.find_local_tree_drift(
                subprojects,
                {"widget-2.0.0"},
            )

            self.assertTrue(any("stale local subproject tree" in item for item in warnings))
            self.assertTrue(any("duplicate local subproject trees" in item for item in warnings))
            self.assertTrue(active.is_dir())
            self.assertTrue(stale.is_dir())


if __name__ == "__main__":
    unittest.main()
