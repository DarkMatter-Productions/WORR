from __future__ import annotations

import base64
import hashlib
import json
import os
import pathlib
import stat
import sys
import tempfile
import unittest
from unittest import mock

from tools.release import materialize_signing_secrets
from tools.release.release_signing import (
    ReleaseSigningError,
    generate_private_key_document,
    load_private_signing_key,
    trust_key_document,
)


class MaterializeSigningSecretsTests(unittest.TestCase):
    @staticmethod
    def trust_policy_bytes() -> bytes:
        public_key = bytes(range(32))
        return (
            json.dumps(
                {
                    "schema_version": 1,
                    "threshold": 1,
                    "keys": [
                        {
                            "key_id": hashlib.sha256(public_key).hexdigest(),
                            "algorithm": "ed25519",
                            "public_key": base64.urlsafe_b64encode(public_key)
                            .rstrip(b"=")
                            .decode("ascii"),
                            "repos": ["example/WORR"],
                            "channels": ["stable"],
                        }
                    ],
                }
            )
            + "\n"
        ).encode("utf-8")

    def test_public_policy_is_decoded_validated_and_exported_without_logging_secret(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            github_env = root / "github-env"
            github_env.touch()
            environment = {
                "TEST_POLICY_B64": base64.b64encode(self.trust_policy_bytes()).decode("ascii")
            }
            argv = [
                "materialize_signing_secrets.py",
                "--trust-policy-env",
                "TEST_POLICY_B64",
                "--repo",
                "example/WORR",
                "--channel",
                "stable",
                "--github-env",
                str(github_env),
                "--temp-root",
                str(root),
            ]

            with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
                sys, "argv", argv
            ):
                self.assertEqual(0, materialize_signing_secrets.main())

            exported = dict(
                line.split("=", 1)
                for line in github_env.read_text(encoding="utf-8").splitlines()
            )
            self.assertEqual(
                {
                    "WORR_RELEASE_SIGNING_SECRET_DIR",
                    "WORR_RELEASE_TRUST_POLICY_PATH",
                },
                set(exported),
            )
            policy_path = pathlib.Path(exported["WORR_RELEASE_TRUST_POLICY_PATH"])
            self.assertEqual(
                policy_path.parent,
                pathlib.Path(exported["WORR_RELEASE_SIGNING_SECRET_DIR"]),
            )
            self.assertEqual(self.trust_policy_bytes(), policy_path.read_bytes())
            if os.name != "nt":
                self.assertEqual(0o600, stat.S_IMODE(policy_path.stat().st_mode))

    def test_absent_or_noncanonical_secret_fails_closed_and_cleans_temp_files(self) -> None:
        for encoded in (None, "not base64", ""):
            with self.subTest(encoded=encoded), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                github_env = root / "github-env"
                github_env.touch()
                environment = {} if encoded is None else {"TEST_POLICY_B64": encoded}
                argv = [
                    "materialize_signing_secrets.py",
                    "--trust-policy-env",
                    "TEST_POLICY_B64",
                    "--repo",
                    "example/WORR",
                    "--channel",
                    "stable",
                    "--github-env",
                    str(github_env),
                    "--temp-root",
                    str(root),
                ]

                with mock.patch.dict(os.environ, environment, clear=True), mock.patch.object(
                    sys, "argv", argv
                ), self.assertRaises(SystemExit):
                    materialize_signing_secrets.main()

                self.assertEqual("", github_env.read_text(encoding="utf-8"))
                self.assertEqual([github_env], list(root.iterdir()))

    def test_existing_policy_file_is_copied_and_validated(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            github_env = root / "github-env"
            github_env.touch()
            source_policy = root / "source-policy.json"
            source_policy.write_bytes(self.trust_policy_bytes())
            argv = [
                "materialize_signing_secrets.py",
                "--trust-policy-file",
                str(source_policy),
                "--repo",
                "example/WORR",
                "--channel",
                "stable",
                "--github-env",
                str(github_env),
                "--temp-root",
                str(root),
            ]

            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(0, materialize_signing_secrets.main())

            exported = dict(
                line.split("=", 1)
                for line in github_env.read_text(encoding="utf-8").splitlines()
            )
            copied_policy = pathlib.Path(exported["WORR_RELEASE_TRUST_POLICY_PATH"])
            self.assertNotEqual(source_policy, copied_policy)
            self.assertEqual(source_policy.read_bytes(), copied_policy.read_bytes())

            hard_link = root / "policy-hard-link.json"
            try:
                os.link(source_policy, hard_link)
            except OSError:
                return
            with self.assertRaisesRegex(SystemExit, "regular non-link"):
                materialize_signing_secrets.read_regular_file(
                    source_policy, "release trust policy"
                )

    def test_private_signing_key_is_validated_and_exported_separately(self) -> None:
        try:
            private_document = generate_private_key_document()
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))
        policy_document = {
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
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            github_env = root / "github-env"
            github_env.touch()
            environment = {
                "TEST_POLICY_B64": base64.b64encode(
                    (json.dumps(policy_document) + "\n").encode("utf-8")
                ).decode("ascii"),
                "TEST_KEY_B64": base64.b64encode(
                    (json.dumps(private_document) + "\n").encode("utf-8")
                ).decode("ascii"),
            }
            argv = [
                "materialize_signing_secrets.py",
                "--trust-policy-env",
                "TEST_POLICY_B64",
                "--signing-key-env",
                "TEST_KEY_B64",
                "--repo",
                "example/WORR",
                "--channel",
                "stable",
                "--github-env",
                str(github_env),
                "--temp-root",
                str(root),
            ]

            with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
                sys, "argv", argv
            ):
                self.assertEqual(0, materialize_signing_secrets.main())

            exported = dict(
                line.split("=", 1)
                for line in github_env.read_text(encoding="utf-8").splitlines()
            )
            self.assertEqual(
                {
                    "WORR_RELEASE_SIGNING_SECRET_DIR",
                    "WORR_RELEASE_TRUST_POLICY_PATH",
                    "WORR_RELEASE_SIGNING_KEY_PATH",
                    "WORR_RELEASE_SIGNING_KEY_LIST_PATH",
                },
                set(exported),
            )
            policy_path = pathlib.Path(exported["WORR_RELEASE_TRUST_POLICY_PATH"])
            key_path = pathlib.Path(exported["WORR_RELEASE_SIGNING_KEY_PATH"])
            key_list_path = pathlib.Path(
                exported["WORR_RELEASE_SIGNING_KEY_LIST_PATH"]
            )
            self.assertNotEqual(policy_path, key_path)
            self.assertNotIn("private_key", policy_path.read_text(encoding="utf-8"))
            self.assertIn("private_key", key_path.read_text(encoding="utf-8"))
            self.assertEqual(
                {
                    "schema_version": 1,
                    "signing_key_paths": [str(key_path)],
                },
                json.loads(key_list_path.read_text(encoding="utf-8")),
            )
            self.assertNotIn(
                "private_key", key_list_path.read_text(encoding="utf-8")
            )
            if os.name != "nt":
                self.assertEqual(0o600, stat.S_IMODE(key_path.stat().st_mode))
                self.assertEqual(0o600, stat.S_IMODE(key_list_path.stat().st_mode))

    def test_versioned_bundle_materializes_distinct_keys_and_meets_threshold(self) -> None:
        try:
            private_documents = [
                generate_private_key_document(),
                generate_private_key_document(),
            ]
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))
        policy_document = {
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
        bundle_document = {
            "schema_version": 1,
            "keys": private_documents,
        }

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            github_env = root / "github-env"
            github_env.touch()
            environment = {
                "TEST_POLICY_B64": base64.b64encode(
                    (json.dumps(policy_document) + "\n").encode("utf-8")
                ).decode("ascii"),
                "TEST_KEY_B64": base64.b64encode(
                    (json.dumps(bundle_document) + "\n").encode("utf-8")
                ).decode("ascii"),
            }
            argv = [
                "materialize_signing_secrets.py",
                "--trust-policy-env",
                "TEST_POLICY_B64",
                "--signing-key-env",
                "TEST_KEY_B64",
                "--repo",
                "example/WORR",
                "--channel",
                "stable",
                "--github-env",
                str(github_env),
                "--temp-root",
                str(root),
            ]

            with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
                sys, "argv", argv
            ):
                self.assertEqual(0, materialize_signing_secrets.main())

            exported = dict(
                line.split("=", 1)
                for line in github_env.read_text(encoding="utf-8").splitlines()
            )
            self.assertEqual(
                {
                    "WORR_RELEASE_SIGNING_SECRET_DIR",
                    "WORR_RELEASE_TRUST_POLICY_PATH",
                    "WORR_RELEASE_SIGNING_KEY_LIST_PATH",
                },
                set(exported),
            )
            key_list_path = pathlib.Path(
                exported["WORR_RELEASE_SIGNING_KEY_LIST_PATH"]
            )
            key_list = json.loads(key_list_path.read_text(encoding="utf-8"))
            self.assertEqual(1, key_list["schema_version"])
            key_paths = [pathlib.Path(value) for value in key_list["signing_key_paths"]]
            self.assertEqual(2, len(key_paths))
            self.assertEqual(2, len(set(key_paths)))
            self.assertTrue(all(path.parent == key_list_path.parent for path in key_paths))
            self.assertEqual(
                {document["key_id"] for document in private_documents},
                {load_private_signing_key(path).key_id for path in key_paths},
            )
            if os.name != "nt":
                self.assertEqual(0o600, stat.S_IMODE(key_list_path.stat().st_mode))
                for path in key_paths:
                    self.assertEqual(0o600, stat.S_IMODE(path.stat().st_mode))

    def test_bundle_rejects_duplicate_unauthorized_and_insufficient_keys(self) -> None:
        try:
            private_documents = [
                generate_private_key_document(),
                generate_private_key_document(),
            ]
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))
        policy_document = {
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
        cases = (
            (
                {"schema_version": 1, "keys": [private_documents[0]] * 2},
                "duplicate signing key identity",
            ),
            (private_documents[0], "threshold requires 2 distinct keys"),
        )
        unauthorized_document = generate_private_key_document()
        cases += (
            (
                {"schema_version": 1, "keys": private_documents + [unauthorized_document]},
                "is not authorized",
            ),
        )

        for signing_document, expected_error in cases:
            with self.subTest(expected_error=expected_error), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                github_env = root / "github-env"
                github_env.touch()
                environment = {
                    "TEST_POLICY_B64": base64.b64encode(
                        (json.dumps(policy_document) + "\n").encode("utf-8")
                    ).decode("ascii"),
                    "TEST_KEY_B64": base64.b64encode(
                        (json.dumps(signing_document) + "\n").encode("utf-8")
                    ).decode("ascii"),
                }
                argv = [
                    "materialize_signing_secrets.py",
                    "--trust-policy-env",
                    "TEST_POLICY_B64",
                    "--signing-key-env",
                    "TEST_KEY_B64",
                    "--repo",
                    "example/WORR",
                    "--channel",
                    "stable",
                    "--github-env",
                    str(github_env),
                    "--temp-root",
                    str(root),
                ]

                with mock.patch.dict(
                    os.environ, environment, clear=False
                ), mock.patch.object(sys, "argv", argv), self.assertRaisesRegex(
                    SystemExit, expected_error
                ):
                    materialize_signing_secrets.main()

                self.assertEqual("", github_env.read_text(encoding="utf-8"))
                self.assertEqual([github_env], list(root.iterdir()))

    def test_bundle_schema_rejects_unknown_fields_and_duplicate_json_keys(self) -> None:
        try:
            private_document = generate_private_key_document()
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))
        invalid_payloads = (
            json.dumps(
                {"schema_version": 1, "keys": [private_document], "extra": True}
            ).encode("utf-8"),
            json.dumps(
                {
                    "schema_version": 1,
                    "keys": [{**private_document, "extra": True}],
                }
            ).encode("utf-8"),
            b'{"schema_version":1,"schema_version":1,"keys":[]}',
        )
        for payload in invalid_payloads:
            with self.subTest(payload=payload), self.assertRaises(ReleaseSigningError):
                materialize_signing_secrets.load_private_key_documents(payload)

    def test_absent_private_key_secret_fails_closed_after_policy_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            github_env = root / "github-env"
            github_env.touch()
            environment = {
                "TEST_POLICY_B64": base64.b64encode(self.trust_policy_bytes()).decode("ascii")
            }
            argv = [
                "materialize_signing_secrets.py",
                "--trust-policy-env",
                "TEST_POLICY_B64",
                "--signing-key-env",
                "MISSING_KEY_B64",
                "--repo",
                "example/WORR",
                "--channel",
                "stable",
                "--github-env",
                str(github_env),
                "--temp-root",
                str(root),
            ]

            with mock.patch.dict(os.environ, environment, clear=True), mock.patch.object(
                sys, "argv", argv
            ), self.assertRaisesRegex(SystemExit, "MISSING_KEY_B64"):
                materialize_signing_secrets.main()

            self.assertEqual("", github_env.read_text(encoding="utf-8"))
            self.assertEqual([github_env], list(root.iterdir()))


if __name__ == "__main__":
    unittest.main()
