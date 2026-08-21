from __future__ import annotations

import json
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

from tools.release import compose_release_index
from tools.release.release_signing import (
    ReleaseSigningError,
    generate_private_key_document,
    trust_key_document,
)


class ComposeReleaseIndexTests(unittest.TestCase):
    @staticmethod
    def write_private_json(path: pathlib.Path, document: object) -> None:
        path.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        if os.name != "nt":
            path.chmod(0o600)

    @staticmethod
    def release_metadata(
        *,
        version: str = "1.2.3",
        channel: str = "stable",
        artifacts: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        return {
            "repo": "example/WORR",
            "channel": channel,
            "version": version,
            "commit_sha": "a" * 40,
            "platform_id": "linux-x86_64",
            "platform_stub": "linux-x86_64",
            "os": "linux",
            "arch": "x86_64",
            "archive_format": "tar.gz",
            "artifacts": artifacts or [],
        }

    @staticmethod
    def unsigned_development_argv(
        artifacts_root: pathlib.Path,
        index_path: pathlib.Path,
        asset_list_path: pathlib.Path,
    ) -> list[str]:
        return [
            "compose_release_index.py",
            "--artifacts-root",
            str(artifacts_root),
            "--repo",
            "example/WORR",
            "--channel",
            "development",
            "--version",
            "1.2.3",
            "--tag",
            "development-test",
            "--commit-sha",
            "a" * 40,
            "--index-path",
            str(index_path),
            "--asset-list-path",
            str(asset_list_path),
            "--allow-unsigned-development-index",
        ]

    def test_cli_release_identity_must_match_platform_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            metadata = {
                "repo": "example/WORR",
                "channel": "development",
                "version": "9.9.9",
                "commit_sha": "a" * 40,
                "artifacts": [],
            }
            (root / "metadata-windows-x86_64.json").write_text(
                json.dumps(metadata),
                encoding="utf-8",
            )
            argv = [
                "compose_release_index.py",
                "--artifacts-root",
                str(root),
                "--repo",
                "example/WORR",
                "--channel",
                "development",
                "--version",
                "1.2.3",
                "--tag",
                "v1.2.3",
                "--commit-sha",
                "a" * 40,
                "--index-path",
                str(root / "index.json"),
                "--asset-list-path",
                str(root / "assets.txt"),
                "--allow-unsigned-development-index",
            ]

            with mock.patch.object(sys, "argv", argv), self.assertRaises(SystemExit):
                compose_release_index.main()

    def test_composer_rejects_nested_metadata_and_cross_bundle_asset_aliases(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            bundle_root = artifacts_root / "selected"
            nested = bundle_root / "nested"
            sibling = artifacts_root / "other"
            nested.mkdir(parents=True)
            sibling.mkdir()

            nested_metadata = nested / "metadata-linux-x86_64.json"
            nested_metadata.write_text(
                json.dumps(self.release_metadata(channel="development")),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(SystemExit, "artifact bundle root"):
                compose_release_index.load_metadata_files(artifacts_root)

            nested_metadata.unlink()
            artifact_name = "package.zip"
            (bundle_root / artifact_name).write_bytes(b"selected payload")
            (sibling / artifact_name).write_bytes(b"different payload")
            (bundle_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(
                    self.release_metadata(
                        channel="development",
                        artifacts=[
                            {
                                "name": artifact_name,
                                "kind": "archive",
                                "role": "client",
                            }
                        ],
                    )
                ),
                encoding="utf-8",
            )
            index_path = root / "index.json"
            asset_list_path = root / "assets.txt"
            argv = self.unsigned_development_argv(
                artifacts_root,
                index_path,
                asset_list_path,
            )

            with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                SystemExit,
                "not unique",
            ):
                compose_release_index.main()

            self.assertFalse(index_path.exists())
            self.assertFalse(asset_list_path.exists())

    def test_composer_rejects_case_colliding_metadata_before_read_or_write(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            first_bundle = artifacts_root / "first"
            second_bundle = artifacts_root / "second"
            first_bundle.mkdir(parents=True)
            second_bundle.mkdir()
            (first_bundle / "metadata-linux-x86_64.json").write_text(
                json.dumps(self.release_metadata(channel="development")),
                encoding="utf-8",
            )
            (second_bundle / "METADATA-LINUX-X86_64.JSON").write_text(
                "not JSON and must not be read",
                encoding="utf-8",
            )
            index_path = root / "index.json"
            asset_list_path = root / "assets.txt"
            argv = self.unsigned_development_argv(
                artifacts_root,
                index_path,
                asset_list_path,
            )

            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    pathlib.Path,
                    "read_text",
                    side_effect=AssertionError("metadata content was read"),
                ),
                self.assertRaisesRegex(SystemExit, "case-insensitive collision"),
            ):
                compose_release_index.main()

            self.assertFalse(index_path.exists())
            self.assertFalse(asset_list_path.exists())

    def test_deferred_and_private_key_modes_are_mutually_exclusive(self) -> None:
        argv = [
            "compose_release_index.py",
            "--artifacts-root",
            "unused",
            "--repo",
            "example/WORR",
            "--channel",
            "stable",
            "--version",
            "1.2.3",
            "--tag",
            "v1.2.3",
            "--commit-sha",
            "a" * 40,
            "--index-path",
            "index.json",
            "--asset-list-path",
            "assets.txt",
            "--trust-policy",
            "policy.json",
            "--signing-key-file",
            "key.json",
            "--defer-signing",
        ]

        with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
            SystemExit,
            "cannot be combined",
        ):
            compose_release_index.main()

    def test_key_path_list_and_direct_key_inputs_combine_without_resolution(self) -> None:
        try:
            private_documents = [
                generate_private_key_document(),
                generate_private_key_document(),
            ]
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            key_paths = [root / "key-1.json", root / "key-2.json"]
            for path, document in zip(key_paths, private_documents, strict=True):
                self.write_private_json(path, document)
            list_path = root / "key-paths.json"
            self.write_private_json(
                list_path,
                {
                    "schema_version": 1,
                    "signing_key_paths": [str(key_paths[1].absolute())],
                },
            )

            self.assertEqual(
                key_paths,
                compose_release_index.collect_signing_key_paths(
                    [str(key_paths[0])], [str(list_path)]
                ),
            )
            self.assertEqual(
                key_paths,
                compose_release_index.collect_signing_key_paths(
                    [str(path) for path in key_paths], []
                ),
            )
            with self.assertRaisesRegex(ReleaseSigningError, "duplicate signing key"):
                compose_release_index.collect_signing_key_paths(
                    [str(key_paths[1])], [str(list_path)]
                )

    def test_key_path_list_rejects_unknown_fields_duplicate_paths_and_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            list_path = root / "key-paths.json"
            invalid_documents = (
                {
                    "schema_version": 1,
                    "signing_key_paths": [str((root / "key.json").absolute())],
                    "extra": True,
                },
                {
                    "schema_version": 1,
                    "signing_key_paths": [
                        str((root / "key.json").absolute()),
                        str((root / "key.json").absolute()),
                    ],
                },
                {
                    "schema_version": 1,
                    "signing_key_paths": [str((root.parent / "outside.json").absolute())],
                },
                {"schema_version": 1, "signing_key_paths": ["relative.json"]},
            )
            for document in invalid_documents:
                with self.subTest(document=document):
                    self.write_private_json(list_path, document)
                    with self.assertRaises(ReleaseSigningError):
                        compose_release_index.load_signing_key_list(list_path)

            list_path.write_text(
                '{"schema_version":1,"schema_version":1,"signing_key_paths":[]}',
                encoding="utf-8",
            )
            if os.name != "nt":
                list_path.chmod(0o600)
            with self.assertRaisesRegex(ReleaseSigningError, "duplicate JSON key"):
                compose_release_index.load_signing_key_list(list_path)

    @unittest.skipIf(os.name == "nt", "symbolic-link creation is not portable on Windows")
    def test_key_path_list_rejects_symbolic_link_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            target = root / "target.json"
            self.write_private_json(
                target,
                {"schema_version": 1, "signing_key_paths": [str(root / "key.json")]},
            )
            link = root / "link.json"
            link.symlink_to(target)
            with self.assertRaisesRegex(ReleaseSigningError, "regular non-link"):
                compose_release_index.load_signing_key_list(link)

    def test_compose_rejects_normalized_output_output_collision_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            (artifacts_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(self.release_metadata(channel="development")),
                encoding="utf-8",
            )
            output_path = root / "output.json"
            aliased_output = root / "unused" / ".." / output_path.name
            argv = self.unsigned_development_argv(
                artifacts_root,
                output_path,
                aliased_output,
            )

            with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                SystemExit,
                "collides",
            ):
                compose_release_index.main()

            self.assertFalse(output_path.exists())

    def test_compose_rejects_portable_case_colliding_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            (artifacts_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(self.release_metadata(channel="development")),
                encoding="utf-8",
            )
            index_path = root / "Release-Output.json"
            asset_list_path = root / "release-output.JSON"
            argv = self.unsigned_development_argv(
                artifacts_root,
                index_path,
                asset_list_path,
            )

            with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                SystemExit,
                "collides",
            ):
                compose_release_index.main()

            self.assertFalse(index_path.exists())
            self.assertFalse(asset_list_path.exists())

    def test_compose_rejects_output_input_collision_without_overwriting_source(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            metadata_path = artifacts_root / "metadata-linux-x86_64.json"
            original_metadata = json.dumps(
                self.release_metadata(channel="development")
            )
            metadata_path.write_text(original_metadata, encoding="utf-8")
            asset_list_path = root / "assets.txt"
            argv = self.unsigned_development_argv(
                artifacts_root,
                metadata_path,
                asset_list_path,
            )

            with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                SystemExit,
                "collides with release metadata input",
            ):
                compose_release_index.main()

            self.assertEqual(
                original_metadata,
                metadata_path.read_text(encoding="utf-8"),
            )
            self.assertFalse(asset_list_path.exists())

    @unittest.skipIf(os.name == "nt", "symbolic-link creation is not portable on Windows")
    def test_compose_rejects_symbolic_link_output_without_touching_target(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            (artifacts_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(self.release_metadata(channel="development")),
                encoding="utf-8",
            )
            target = root / "target.json"
            target.write_text("do not replace", encoding="utf-8")
            index_link = root / "index.json"
            index_link.symlink_to(target)
            argv = self.unsigned_development_argv(
                artifacts_root,
                index_link,
                root / "assets.txt",
            )

            with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                SystemExit,
                "regular non-link",
            ):
                compose_release_index.main()

            self.assertEqual("do not replace", target.read_text(encoding="utf-8"))

    @unittest.skipUnless(hasattr(os, "link"), "hard links are unavailable")
    def test_compose_rejects_hard_linked_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            (artifacts_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(self.release_metadata(channel="development")),
                encoding="utf-8",
            )
            index_path = root / "index.json"
            index_path.write_text("do not replace", encoding="utf-8")
            asset_list_path = root / "assets.txt"
            os.link(index_path, asset_list_path)
            argv = self.unsigned_development_argv(
                artifacts_root,
                index_path,
                asset_list_path,
            )

            with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                SystemExit,
                "hard links",
            ):
                compose_release_index.main()

            self.assertEqual("do not replace", index_path.read_text(encoding="utf-8"))
            self.assertEqual(
                "do not replace",
                asset_list_path.read_text(encoding="utf-8"),
            )

    @unittest.skipUnless(hasattr(os, "link"), "hard links are unavailable")
    def test_compose_rejects_hard_linked_artifact_and_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            artifact_path = artifacts_root / "package.zip"
            artifact_path.write_bytes(b"authenticated package bytes")
            metadata = self.release_metadata(
                channel="development",
                artifacts=[
                    {"name": artifact_path.name, "kind": "archive", "role": "client"}
                ],
            )
            (artifacts_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(metadata),
                encoding="utf-8",
            )
            index_path = root / "index.json"
            os.link(artifact_path, index_path)
            argv = self.unsigned_development_argv(
                artifacts_root,
                index_path,
                root / "assets.txt",
            )

            with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                SystemExit,
                "hard links",
            ):
                compose_release_index.main()

            self.assertEqual(b"authenticated package bytes", artifact_path.read_bytes())
            self.assertEqual(b"authenticated package bytes", index_path.read_bytes())

    def test_signature_output_cannot_alias_release_artifact(self) -> None:
        try:
            private_document = generate_private_key_document()
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            index_path = artifacts_root / "index.json"
            artifact_path = pathlib.Path(f"{index_path}.sig.json")
            artifact_path.write_bytes(b"authenticated package bytes")
            (artifacts_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(
                    self.release_metadata(
                        artifacts=[
                            {
                                "name": artifact_path.name,
                                "kind": "archive",
                                "role": "client",
                            }
                        ]
                    )
                ),
                encoding="utf-8",
            )
            key_path = root / "key.json"
            self.write_private_json(key_path, private_document)
            policy_path = root / "trust-policy.json"
            policy_path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "threshold": 1,
                        "keys": [
                            trust_key_document(
                                private_document,
                                repos=["example/WORR"],
                                channels=["stable"],
                            )
                        ],
                    }
                ),
                encoding="utf-8",
            )
            argv = [
                "compose_release_index.py",
                "--artifacts-root",
                str(artifacts_root),
                "--repo",
                "example/WORR",
                "--channel",
                "stable",
                "--version",
                "1.2.3",
                "--tag",
                "v1.2.3",
                "--commit-sha",
                "a" * 40,
                "--index-path",
                str(index_path),
                "--asset-list-path",
                str(root / "assets.txt"),
                "--signature-path",
                str(artifact_path),
                "--signing-key-file",
                str(key_path),
                "--trust-policy",
                str(policy_path),
            ]

            with mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                SystemExit,
                "release signature output collides with release artifact",
            ):
                compose_release_index.main()

            self.assertFalse(index_path.exists())
            self.assertEqual(b"authenticated package bytes", artifact_path.read_bytes())

    def test_compose_signs_with_threshold_bundle_path_list(self) -> None:
        try:
            private_documents = [
                generate_private_key_document(),
                generate_private_key_document(),
            ]
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            (artifacts_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(self.release_metadata()),
                encoding="utf-8",
            )
            key_paths = [root / "key-1.json", root / "key-2.json"]
            for path, document in zip(key_paths, private_documents, strict=True):
                self.write_private_json(path, document)
            list_path = root / "key-paths.json"
            self.write_private_json(
                list_path,
                {
                    "schema_version": 1,
                    "signing_key_paths": [str(path.absolute()) for path in key_paths],
                },
            )
            policy_path = root / "trust-policy.json"
            policy_path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "threshold": 2,
                        "keys": [
                            trust_key_document(
                                document,
                                repos=["example/WORR"],
                                channels=["stable"],
                            )
                            for document in private_documents
                        ],
                    }
                ),
                encoding="utf-8",
            )
            index_path = root / "index.json"
            asset_list_path = root / "assets.txt"
            argv = [
                "compose_release_index.py",
                "--artifacts-root",
                str(artifacts_root),
                "--repo",
                "example/WORR",
                "--channel",
                "stable",
                "--version",
                "1.2.3",
                "--tag",
                "v1.2.3",
                "--commit-sha",
                "a" * 40,
                "--index-path",
                str(index_path),
                "--asset-list-path",
                str(asset_list_path),
                "--signing-key-list",
                str(list_path),
                "--trust-policy",
                str(policy_path),
            ]

            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(0, compose_release_index.main())

            signature_path = pathlib.Path(f"{index_path}.sig.json")
            signature = json.loads(signature_path.read_text(encoding="utf-8"))
            self.assertEqual(2, len(signature["signatures"]))
            listed_paths = {
                (asset_list_path.parent / value).resolve()
                for value in asset_list_path.read_text(encoding="utf-8").splitlines()
            }
            self.assertIn(index_path, listed_paths)
            self.assertIn(signature_path, listed_paths)

    def test_index_contains_signed_freshness_sequence_and_revocations(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            (artifacts_root / "metadata-linux-x86_64.json").write_text(
                json.dumps(self.release_metadata(channel="development")),
                encoding="utf-8",
            )
            revoked_digest = "ab" * 32
            revocation_path = root / "revocations.json"
            revocation_path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "channels": {"development": [revoked_digest]},
                    }
                ),
                encoding="utf-8",
            )
            index_path = root / "index.json"
            argv = self.unsigned_development_argv(
                artifacts_root, index_path, root / "assets.txt"
            )
            argv.extend(
                [
                    "--release-sequence",
                    "42",
                    "--valid-for-days",
                    "7",
                    "--revocation-file",
                    str(revocation_path),
                ]
            )

            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(0, compose_release_index.main())

            index = json.loads(index_path.read_text(encoding="utf-8"))
            self.assertEqual(42, index["release_sequence"])
            self.assertEqual(
                [revoked_digest], index["revoked_release_index_sha256s"]
            )
            generated = compose_release_index.dt.datetime.strptime(
                index["generated_at_utc"], "%Y-%m-%dT%H:%M:%SZ"
            )
            expires = compose_release_index.dt.datetime.strptime(
                index["expires_at_utc"], "%Y-%m-%dT%H:%M:%SZ"
            )
            self.assertEqual(7, (expires - generated).days)

    def test_revocation_list_rejects_duplicates_and_noncanonical_digests(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "revocations.json"
            invalid_lists = (["ab" * 32, "ab" * 32], ["AB" * 32], ["short"])
            for values in invalid_lists:
                with self.subTest(values=values):
                    path.write_text(
                        json.dumps(
                            {
                                "schema_version": 1,
                                "channels": {"stable": values},
                            }
                        ),
                        encoding="utf-8",
                    )
                    with self.assertRaises(ReleaseSigningError):
                        compose_release_index.load_release_revocations(
                            path, "stable"
                        )

    def test_composer_rejects_native_incompatible_artifact_sizes(self) -> None:
        with self.assertRaisesRegex(ReleaseSigningError, "GitHub release assets"):
            compose_release_index.validate_release_artifact_size(
                pathlib.Path("package.zip"),
                "archive",
                compose_release_index.MAX_UPDATE_PACKAGE_BYTES + 1,
            )
        with self.assertRaisesRegex(ReleaseSigningError, "exceeds 32 MiB"):
            compose_release_index.validate_release_artifact_size(
                pathlib.Path("manifest.json"),
                "manifest",
                compose_release_index.MAX_RELEASE_METADATA_BYTES + 1,
            )


if __name__ == "__main__":
    unittest.main()
