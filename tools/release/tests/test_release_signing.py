from __future__ import annotations

import json
import hashlib
import io
import datetime as dt
import os
import pathlib
import shutil
import sys
import tempfile
import unittest
from dataclasses import replace
from unittest import mock

from tools.release import compose_release_index, release_signing
from tools.release.release_signing import (
    MAX_POLICY_KEYS,
    MAX_SCOPES_PER_KEY,
    MAX_SIGNATURE_ENVELOPE_SIZE,
    MAX_SIGNATURES,
    PrivateSigningKey,
    ReleaseSigningError,
    aggregate_signature_envelopes,
    create_signature_envelope,
    generate_private_key_document,
    load_private_signing_key,
    load_trust_policy,
    serialize_signature_envelope,
    trust_key_document,
    verify_signature_envelope,
)


REPO = "example/WORR"
CHANNEL = "stable"


def release_index_payload(*, repo: str = REPO, channel: str = CHANNEL) -> bytes:
    generated = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
    expires = generated + dt.timedelta(days=14)
    return (
        json.dumps(
            {
                "schema_version": 3,
                "repo": repo,
                "channel": channel,
                "version": "1.2.3",
                "tag": "v1.2.3",
                "commit_sha": "a" * 40,
                "generated_at_utc": generated.strftime("%Y-%m-%dT%H:%M:%SZ"),
                "expires_at_utc": expires.strftime("%Y-%m-%dT%H:%M:%SZ"),
                "release_sequence": 1,
                "revoked_release_index_sha256s": [],
                "targets": [],
            },
            indent=2,
        )
        + "\n"
    ).encode("utf-8")


class ReleaseSigningTests(unittest.TestCase):
    def setUp(self) -> None:
        try:
            self.key_document = generate_private_key_document()
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))

    @staticmethod
    def write_json(path: pathlib.Path, value: object, *, private: bool = False) -> None:
        path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
        if private and os.name != "nt":
            path.chmod(0o600)

    def materialize_key(
        self,
        root: pathlib.Path,
        document: dict | None = None,
        *,
        name: str = "key.json",
    ):
        value = document or self.key_document
        path = root / name
        self.write_json(path, value, private=True)
        return load_private_signing_key(path)

    def materialize_policy(
        self,
        root: pathlib.Path,
        documents: list[dict] | None = None,
        *,
        threshold: int = 1,
    ):
        documents = documents or [self.key_document]
        policy_document = {
            "schema_version": 1,
            "threshold": threshold,
            "keys": [
                trust_key_document(document, repos=[REPO], channels=[CHANNEL])
                for document in documents
            ],
        }
        path = root / "trust-policy.json"
        self.write_json(path, policy_document)
        return load_trust_policy(path)

    def candidate_validation_args(
        self,
        root: pathlib.Path,
        artifacts_root: pathlib.Path,
        index_path: pathlib.Path,
        *,
        valid_for_days: int = 180,
    ) -> tuple[list[str], int]:
        revocation_path = root / "trusted-revocations.json"
        self.write_json(
            revocation_path,
            {"schema_version": 1, "channels": {CHANNEL: []}},
        )
        sequence = json.loads(index_path.read_text(encoding="utf-8"))[
            "release_sequence"
        ]
        return (
            [
                "--artifacts-root",
                str(artifacts_root),
                "--repo",
                REPO,
                "--channel",
                CHANNEL,
                "--version",
                "1.2.3",
                "--tag",
                "v1.2.3",
                "--commit-sha",
                "a" * 40,
                "--release-sequence",
                str(sequence),
                "--valid-for-days",
                str(valid_for_days),
                "--revocation-file",
                str(revocation_path),
            ],
            sequence,
        )

    def test_exact_payload_and_identity_verify(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            key = self.materialize_key(root)
            policy = self.materialize_policy(root)
            payload = release_index_payload()
            envelope = serialize_signature_envelope(
                create_signature_envelope(payload, "index.json", [key])
            )

            verified = verify_signature_envelope(
                payload,
                "index.json",
                envelope,
                policy,
                expected_repo=REPO,
                expected_channel=CHANNEL,
            )

            self.assertEqual(verified.payload, payload)
            self.assertEqual(verified.index["version"], "1.2.3")
            self.assertEqual(verified.verified_key_ids, (key.key_id,))

    def test_canonical_publish_asset_limit_is_exact(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            release_assets = tuple(
                root / f"asset-{index:04d}.bin"
                for index in range(release_signing.MAX_SIGNING_REQUEST_ASSETS)
            )
            request = release_signing.OfflineSigningRequest(
                request_path=root / "request.json",
                request_payload=b"{}\n",
                trust_policy_path=root / "policy.json",
                trust_policy_payload=b"{}\n",
                payload_path=release_assets[0],
                payload=b"{}\n",
                signature_path=root / "index.json.sig.json",
                authorization_path=root / "request.json.authorization.sig.json",
                asset_list_path=root / "assets.txt",
                release_asset_paths=release_assets,
                release_asset_snapshots=(),
                trust_policy=release_signing.TrustPolicy(1, {}),
                repo=REPO,
                channel=CHANNEL,
            )
            payload, paths = release_signing.canonical_request_asset_list(request)
            self.assertEqual(release_signing.MAX_PUBLISHED_ASSETS, len(paths))
            self.assertEqual(
                release_signing.MAX_PUBLISHED_ASSETS,
                len(payload.splitlines()),
            )

            over_bound = replace(
                request,
                release_asset_paths=release_assets + (root / "overflow.bin",),
            )
            with self.assertRaisesRegex(ReleaseSigningError, "exceeds 1000 assets"):
                release_signing.canonical_request_asset_list(over_bound)

    def test_payload_tampering_and_asset_renaming_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            key = self.materialize_key(root)
            policy = self.materialize_policy(root)
            payload = release_index_payload()
            envelope = serialize_signature_envelope(
                create_signature_envelope(payload, "index.json", [key])
            )

            with self.assertRaisesRegex(ReleaseSigningError, "sha256 mismatch"):
                verify_signature_envelope(
                    payload.replace(b"1.2.3", b"9.9.9"),
                    "index.json",
                    envelope,
                    policy,
                )
            with self.assertRaisesRegex(ReleaseSigningError, "name mismatch"):
                verify_signature_envelope(payload, "renamed.json", envelope, policy)

            tampered_payload = payload.replace(b"1.2.3", b"9.9.9")
            tampered_envelope = json.loads(envelope)
            tampered_envelope["payload"]["size"] = len(tampered_payload)
            tampered_envelope["payload"]["sha256"] = hashlib.sha256(
                tampered_payload
            ).hexdigest()
            with self.assertRaisesRegex(ReleaseSigningError, "threshold not met"):
                verify_signature_envelope(
                    tampered_payload,
                    "index.json",
                    serialize_signature_envelope(tampered_envelope),
                    policy,
                )

    def test_key_scope_is_enforced(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            key = self.materialize_key(root)
            policy_document = {
                "schema_version": 1,
                "threshold": 1,
                "keys": [
                    trust_key_document(
                        self.key_document,
                        repos=["different/WORR"],
                        channels=[CHANNEL],
                    )
                ],
            }
            policy_path = root / "wrong-scope.json"
            self.write_json(policy_path, policy_document)
            payload = release_index_payload()
            envelope = serialize_signature_envelope(
                create_signature_envelope(payload, "index.json", [key])
            )

            with self.assertRaisesRegex(ReleaseSigningError, "threshold not met"):
                verify_signature_envelope(
                    payload,
                    "index.json",
                    envelope,
                    load_trust_policy(policy_path),
                )

    def test_overlap_rotation_and_threshold_policy(self) -> None:
        second_document = generate_private_key_document()
        unknown_document = generate_private_key_document()
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first_key = self.materialize_key(root, name="first.json")
            second_key = self.materialize_key(root, second_document, name="second.json")
            unknown_key = self.materialize_key(root, unknown_document, name="unknown.json")
            payload = release_index_payload()
            policy = self.materialize_policy(
                root, [self.key_document, second_document], threshold=2
            )

            dual_envelope = serialize_signature_envelope(
                create_signature_envelope(
                    payload, "index.json", [first_key, second_key, unknown_key]
                )
            )
            verified = verify_signature_envelope(
                payload, "index.json", dual_envelope, policy
            )
            self.assertEqual(
                set(verified.verified_key_ids), {first_key.key_id, second_key.key_id}
            )

            single_envelope = serialize_signature_envelope(
                create_signature_envelope(payload, "index.json", [second_key])
            )
            with self.assertRaisesRegex(ReleaseSigningError, "threshold not met"):
                verify_signature_envelope(payload, "index.json", single_envelope, policy)

    def test_independent_signature_envelopes_aggregate_under_threshold_policy(self) -> None:
        second_document = generate_private_key_document()
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first_key = self.materialize_key(root, name="first.json")
            second_key = self.materialize_key(
                root,
                second_document,
                name="second.json",
            )
            policy = self.materialize_policy(
                root,
                [self.key_document, second_document],
                threshold=2,
            )
            payload = release_index_payload()
            partials = [
                serialize_signature_envelope(
                    create_signature_envelope(payload, "index.json", [key])
                )
                for key in (second_key, first_key)
            ]

            aggregate = aggregate_signature_envelopes(
                payload,
                "index.json",
                partials,
                policy,
                expected_repo=REPO,
                expected_channel=CHANNEL,
            )
            verified = verify_signature_envelope(
                payload,
                "index.json",
                serialize_signature_envelope(aggregate),
                policy,
                expected_repo=REPO,
                expected_channel=CHANNEL,
            )

            self.assertEqual(
                [entry["key_id"] for entry in aggregate["signatures"]],
                sorted((first_key.key_id, second_key.key_id)),
            )
            self.assertEqual(
                verified.verified_key_ids,
                tuple(sorted((first_key.key_id, second_key.key_id))),
            )

    def test_signature_aggregation_rejects_mismatch_duplicate_and_partial_threshold(self) -> None:
        second_document = generate_private_key_document()
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first_key = self.materialize_key(root, name="first.json")
            second_key = self.materialize_key(
                root,
                second_document,
                name="second.json",
            )
            policy = self.materialize_policy(
                root,
                [self.key_document, second_document],
                threshold=2,
            )
            payload = release_index_payload()
            first_partial = serialize_signature_envelope(
                create_signature_envelope(payload, "index.json", [first_key])
            )
            second_partial = serialize_signature_envelope(
                create_signature_envelope(payload, "index.json", [second_key])
            )
            mismatched_partial = serialize_signature_envelope(
                create_signature_envelope(
                    release_index_payload(channel="nightly"),
                    "index.json",
                    [second_key],
                )
            )

            with self.assertRaisesRegex(ReleaseSigningError, "exact release index"):
                aggregate_signature_envelopes(
                    payload,
                    "index.json",
                    [first_partial, mismatched_partial],
                    policy,
                )
            with self.assertRaisesRegex(ReleaseSigningError, "repeat key"):
                aggregate_signature_envelopes(
                    payload,
                    "index.json",
                    [first_partial, first_partial],
                    policy,
                )
            with self.assertRaisesRegex(ReleaseSigningError, "threshold not met"):
                aggregate_signature_envelopes(
                    payload,
                    "index.json",
                    [second_partial],
                    policy,
                )

    def test_aggregate_cli_combines_custodian_envelopes(self) -> None:
        second_document = generate_private_key_document()
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first_key = self.materialize_key(root, name="first.json")
            second_key = self.materialize_key(
                root,
                second_document,
                name="second.json",
            )
            policy = self.materialize_policy(
                root,
                [self.key_document, second_document],
                threshold=2,
            )
            payload_path = root / "index.json"
            payload_path.write_bytes(release_index_payload())
            partial_paths = [root / "first.sig.json", root / "second.sig.json"]
            for path, key in zip(
                partial_paths,
                (first_key, second_key),
                strict=True,
            ):
                path.write_bytes(
                    serialize_signature_envelope(
                        create_signature_envelope(
                            payload_path.read_bytes(),
                            payload_path.name,
                            [key],
                        )
                    )
                )
            output_path = root / "index.sig.json"
            argv = [
                "release_signing.py",
                "aggregate",
                "--payload",
                str(payload_path),
                "--signature",
                str(partial_paths[0]),
                "--signature",
                str(partial_paths[1]),
                "--output-signature",
                str(output_path),
                "--trust-policy",
                str(root / "trust-policy.json"),
                "--repo",
                REPO,
                "--channel",
                CHANNEL,
            ]

            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(0, release_signing.main())

            verified = verify_signature_envelope(
                payload_path.read_bytes(),
                payload_path.name,
                output_path.read_bytes(),
                policy,
                expected_repo=REPO,
                expected_channel=CHANNEL,
            )
            self.assertEqual(2, len(verified.verified_key_ids))

    def test_deferred_composition_becomes_publish_ready_only_after_threshold_aggregation(
        self,
    ) -> None:
        second_document = generate_private_key_document()
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifacts_root = root / "artifacts"
            artifacts_root.mkdir()
            metadata = {
                "repo": REPO,
                "channel": CHANNEL,
                "version": "1.2.3",
                "commit_sha": "a" * 40,
                "platform_id": "test-platform",
                "platform_stub": "test",
                "os": "test",
                "arch": "test",
                "archive_format": "zip",
                "autoupdater": {},
                "roles": {},
                "artifacts": [],
            }
            self.write_json(
                artifacts_root / "metadata-test-platform.json",
                metadata,
            )
            first_key = self.materialize_key(root, name="first.json")
            second_key = self.materialize_key(
                root,
                second_document,
                name="second.json",
            )
            policy = self.materialize_policy(
                root,
                [self.key_document, second_document],
                threshold=2,
            )
            index_path = root / "worr-release-index-stable.json"
            signature_path = pathlib.Path(f"{index_path}.sig.json")
            request_path = pathlib.Path(f"{index_path}.signing-request.json")
            asset_list_path = root / "release-assets.txt"
            compose_argv = [
                "compose_release_index.py",
                "--artifacts-root",
                str(artifacts_root),
                "--repo",
                REPO,
                "--channel",
                CHANNEL,
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
                "--trust-policy",
                str(root / "trust-policy.json"),
                "--defer-signing",
            ]

            with mock.patch.object(sys, "argv", compose_argv):
                self.assertEqual(0, compose_release_index.main())

            self.assertTrue(index_path.is_file())
            self.assertTrue(request_path.is_file())
            self.assertFalse(signature_path.exists())
            self.assertFalse(asset_list_path.exists())
            request_document = json.loads(request_path.read_text(encoding="utf-8"))
            index_payload = index_path.read_bytes()
            candidate_args, release_sequence = self.candidate_validation_args(
                root,
                artifacts_root,
                index_path,
            )
            with mock.patch.object(
                sys,
                "argv",
                [
                    "release_signing.py",
                    "verify-candidate",
                    "--signing-request",
                    str(request_path),
                    *candidate_args,
                ],
            ):
                self.assertEqual(0, release_signing.main())

            candidate_request = release_signing.load_offline_signing_request(
                request_path
            )
            sequence_tamper = json.loads(candidate_request.payload)
            sequence_tamper["release_sequence"] = (1 << 64) - 1
            with self.assertRaisesRegex(ReleaseSigningError, "release_sequence mismatch"):
                release_signing.validate_candidate_signing_request(
                    replace(
                        candidate_request,
                        payload=(json.dumps(sequence_tamper) + "\n").encode(),
                    ),
                    artifacts_root=artifacts_root,
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                    expected_valid_for_days=180,
                    revocation_file=root / "trusted-revocations.json",
                )
            target_tamper = json.loads(candidate_request.payload)
            target_tamper["targets"][0]["os"] = "remapped-os"
            with self.assertRaisesRegex(ReleaseSigningError, "independent platform metadata"):
                release_signing.validate_candidate_signing_request(
                    replace(
                        candidate_request,
                        payload=(json.dumps(target_tamper) + "\n").encode(),
                    ),
                    artifacts_root=artifacts_root,
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                    expected_valid_for_days=180,
                    revocation_file=root / "trusted-revocations.json",
                )
            authorization_time = dt.datetime.now(dt.timezone.utc).replace(
                microsecond=0
            )
            aged_tamper = json.loads(candidate_request.payload)
            aged_generated = authorization_time - dt.timedelta(days=179)
            aged_tamper["generated_at_utc"] = aged_generated.strftime(
                "%Y-%m-%dT%H:%M:%SZ"
            )
            aged_tamper["expires_at_utc"] = (
                aged_generated + dt.timedelta(days=180)
            ).strftime("%Y-%m-%dT%H:%M:%SZ")
            with self.assertRaisesRegex(ReleaseSigningError, "authorization window"):
                release_signing.validate_candidate_signing_request(
                    replace(
                        candidate_request,
                        payload=(json.dumps(aged_tamper) + "\n").encode(),
                    ),
                    artifacts_root=artifacts_root,
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                    expected_valid_for_days=180,
                    revocation_file=root / "trusted-revocations.json",
                    now=authorization_time,
                )
            self.assertEqual(2, request_document["schema_version"])
            self.assertFalse(pathlib.Path(request_document["payload"]["path"]).is_absolute())
            self.assertFalse(
                pathlib.Path(request_document["trust_policy"]["path"]).is_absolute()
            )
            self.assertEqual("awaiting-signatures", request_document["status"])
            self.assertEqual(len(index_payload), request_document["payload"]["size"])
            self.assertEqual(
                hashlib.sha256(index_payload).hexdigest(),
                request_document["payload"]["sha256"],
            )
            self.assertEqual(2, request_document["trust_policy"]["threshold"])

            index_path.write_bytes(index_payload + b" ")
            with self.assertRaisesRegex(ReleaseSigningError, "payload bytes"):
                release_signing.load_offline_signing_request(request_path)
            index_path.write_bytes(index_payload)

            key_list_path = root / "signing-key-paths.json"
            self.write_json(
                key_list_path,
                {
                    "schema_version": 1,
                    "signing_key_paths": [
                        str((root / "first.json").absolute()),
                        str((root / "second.json").absolute()),
                    ],
                },
                private=True,
            )
            authorized_partial = root / "authorized.partial.sig.json"
            sign_request_argv = [
                "release_signing.py",
                "sign-request",
                "--signing-request",
                str(request_path),
                "--signature",
                str(authorized_partial),
                *candidate_args,
                "--signing-key-list",
                str(key_list_path),
            ]
            with mock.patch.object(sys, "argv", sign_request_argv):
                self.assertEqual(0, release_signing.main())

            incomplete_partial = root / "incomplete.partial.sig.json"
            incomplete_sign_argv = [
                "release_signing.py",
                "sign-request",
                "--signing-request",
                str(request_path),
                "--signature",
                str(incomplete_partial),
                *candidate_args,
                "--signing-key-file",
                str(root / "first.json"),
            ]
            with mock.patch.object(sys, "argv", incomplete_sign_argv):
                self.assertEqual(0, release_signing.main())

            incomplete_argv = [
                "release_signing.py",
                "aggregate",
                "--signing-request",
                str(request_path),
                "--signature",
                str(incomplete_partial),
            ]
            with mock.patch.object(sys, "argv", incomplete_argv), mock.patch.object(
                sys,
                "stderr",
                io.StringIO(),
            ):
                self.assertEqual(1, release_signing.main())
            self.assertFalse(signature_path.exists())
            authorization_path = pathlib.Path(
                f"{request_path}.authorization.sig.json"
            )
            self.assertFalse(authorization_path.exists())
            self.assertFalse(asset_list_path.exists())

            complete_argv = [
                "release_signing.py",
                "aggregate",
                "--signing-request",
                str(request_path),
                "--signature",
                str(authorized_partial),
            ]
            with mock.patch.object(sys, "argv", complete_argv):
                self.assertEqual(0, release_signing.main())

            verified = verify_signature_envelope(
                index_payload,
                index_path.name,
                signature_path.read_bytes(),
                policy,
                expected_repo=REPO,
                expected_channel=CHANNEL,
            )
            self.assertEqual(2, len(verified.verified_key_ids))
            release_assets = {
                (asset_list_path.parent / line).resolve()
                for line in asset_list_path.read_text(encoding="utf-8").splitlines()
            }
            self.assertEqual(
                {
                    index_path,
                    request_path,
                    signature_path,
                    authorization_path,
                    root / "trust-policy.json",
                },
                release_assets,
            )
            self.assertIn(request_path, release_assets)
            authorization = release_signing.verify_publish_authorization(
                signing_request_path=request_path,
                signature_path=signature_path,
                authorization_path=authorization_path,
                asset_list_path=asset_list_path,
                trust_policy_path=root / "trust-policy.json",
                expected_repo=REPO,
                expected_channel=CHANNEL,
                expected_version="1.2.3",
                expected_tag="v1.2.3",
                expected_commit_sha="a" * 40,
                expected_release_sequence=release_sequence,
            )
            self.assertEqual(5, len(authorization.authorized_asset_paths))
            with self.assertRaisesRegex(ReleaseSigningError, "authorization window"):
                release_signing.verify_publish_authorization(
                    signing_request_path=request_path,
                    signature_path=signature_path,
                    authorization_path=authorization_path,
                    asset_list_path=asset_list_path,
                    trust_policy_path=root / "trust-policy.json",
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                    now=authorization_time + dt.timedelta(hours=24, seconds=1),
                )
            with mock.patch.object(
                release_signing,
                "load_trust_policy",
                side_effect=AssertionError("policy path was reopened after byte comparison"),
            ):
                release_signing.verify_publish_authorization(
                    signing_request_path=request_path,
                    signature_path=signature_path,
                    authorization_path=authorization_path,
                    asset_list_path=asset_list_path,
                    trust_policy_path=root / "trust-policy.json",
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                )
            authorization_verified = False
            original_verify_request = (
                release_signing.verify_request_authorization_envelope
            )
            original_sha256_file = release_signing._sha256_file

            def mark_authorization_verified(*args, **kwargs):
                nonlocal authorization_verified
                result = original_verify_request(*args, **kwargs)
                authorization_verified = True
                return result

            def reject_post_verification_reopen(*args, **kwargs):
                if authorization_verified:
                    raise AssertionError(
                        "authorized asset path reopened after verification snapshot"
                    )
                return original_sha256_file(*args, **kwargs)

            with mock.patch.object(
                release_signing,
                "verify_request_authorization_envelope",
                side_effect=mark_authorization_verified,
            ), mock.patch.object(
                release_signing,
                "_sha256_file",
                side_effect=reject_post_verification_reopen,
            ):
                release_signing.verify_publish_authorization(
                    signing_request_path=request_path,
                    signature_path=signature_path,
                    authorization_path=authorization_path,
                    asset_list_path=asset_list_path,
                    trust_policy_path=root / "trust-policy.json",
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                )

            original_request_payload = request_path.read_bytes()
            original_policy_payload = (root / "trust-policy.json").read_bytes()
            request_value = json.loads(original_request_payload)

            extra_path = root / "unindexed-extra.bin"
            extra_path.write_bytes(b"not described by the signed index")
            extra_request = json.loads(json.dumps(request_value))
            extra_request["release_assets"].append(
                {
                    "path": extra_path.name,
                    "size": extra_path.stat().st_size,
                    "sha256": hashlib.sha256(extra_path.read_bytes()).hexdigest(),
                }
            )
            extra_request["release_assets"].sort(
                key=lambda value: (value["path"].casefold(), value["path"])
            )
            request_path.write_bytes(
                (json.dumps(extra_request, indent=2, sort_keys=True) + "\n").encode()
            )
            with self.assertRaisesRegex(ReleaseSigningError, "exact set described"):
                release_signing.verify_publish_authorization(
                    signing_request_path=request_path,
                    signature_path=signature_path,
                    authorization_path=authorization_path,
                    asset_list_path=asset_list_path,
                    trust_policy_path=root / "trust-policy.json",
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                )
            request_path.write_bytes(original_request_payload)

            (root / "trust-policy.json").write_bytes(original_policy_payload + b" ")
            with self.assertRaisesRegex(ReleaseSigningError, "policy bytes changed"):
                release_signing.verify_publish_authorization(
                    signing_request_path=request_path,
                    signature_path=signature_path,
                    authorization_path=authorization_path,
                    asset_list_path=asset_list_path,
                    trust_policy_path=root / "trust-policy.json",
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                )
            (root / "trust-policy.json").write_bytes(original_policy_payload)

            alternate_list = root / "alternate-assets.txt"
            alternate_list.write_bytes(asset_list_path.read_bytes())
            output_tamper = json.loads(json.dumps(request_value))
            output_tamper["outputs"]["asset_list_path"] = alternate_list.name
            request_path.write_bytes(
                (json.dumps(output_tamper, indent=2, sort_keys=True) + "\n").encode()
            )
            with self.assertRaisesRegex(ReleaseSigningError, "exact signing request"):
                release_signing.verify_publish_authorization(
                    signing_request_path=request_path,
                    signature_path=signature_path,
                    authorization_path=authorization_path,
                    asset_list_path=alternate_list,
                    trust_policy_path=root / "trust-policy.json",
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                    expected_version="1.2.3",
                    expected_tag="v1.2.3",
                    expected_commit_sha="a" * 40,
                    expected_release_sequence=release_sequence,
                )
            request_path.write_bytes(original_request_payload)

    def test_signing_request_is_relocatable_and_rejects_portability_aliases(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            container = pathlib.Path(temp)
            source = container / "candidate"
            artifacts = source / "artifacts"
            artifacts.mkdir(parents=True)
            self.write_json(
                artifacts / "metadata-test-platform.json",
                {
                    "repo": REPO,
                    "channel": CHANNEL,
                    "version": "1.2.3",
                    "commit_sha": "a" * 40,
                    "platform_id": "test-platform",
                    "platform_stub": "test",
                    "os": "test",
                    "arch": "test",
                    "archive_format": "zip",
                    "autoupdater": {},
                    "roles": {},
                    "artifacts": [],
                },
            )
            self.materialize_policy(source)
            index_path = source / "worr-release-index-stable.json"
            request_path = source / "release-signing-request.json"
            argv = [
                "compose_release_index.py",
                "--artifacts-root",
                str(artifacts),
                "--repo",
                REPO,
                "--channel",
                CHANNEL,
                "--version",
                "1.2.3",
                "--tag",
                "v1.2.3",
                "--commit-sha",
                "a" * 40,
                "--index-path",
                str(index_path),
                "--asset-list-path",
                str(source / "release-assets.txt"),
                "--trust-policy",
                str(source / "trust-policy.json"),
                "--defer-signing",
                "--signing-request-path",
                str(request_path),
            ]
            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(0, compose_release_index.main())

            candidate_args, release_sequence = self.candidate_validation_args(
                container,
                artifacts,
                index_path,
            )

            key_path = source / "relocation-key.json"
            self.write_json(key_path, self.key_document, private=True)
            partial_path = container / "relocation-partial.json"
            with mock.patch.object(
                sys,
                "argv",
                [
                    "release_signing.py",
                    "sign-request",
                    "--signing-request",
                    str(request_path),
                    "--signature",
                    str(partial_path),
                    *candidate_args,
                    "--signing-key-file",
                    str(key_path),
                ],
            ):
                self.assertEqual(0, release_signing.main())
            with mock.patch.object(
                sys,
                "argv",
                [
                    "release_signing.py",
                    "aggregate",
                    "--signing-request",
                    str(request_path),
                    "--signature",
                    str(partial_path),
                ],
            ):
                self.assertEqual(0, release_signing.main())
            frozen_policy = container / "frozen-policy.json"
            frozen_policy.write_bytes((source / "trust-policy.json").read_bytes())

            moved = container / "relocated"
            shutil.move(source, moved)
            moved_request = moved / request_path.name
            loaded = release_signing.load_offline_signing_request(moved_request)
            self.assertEqual(moved / index_path.name, loaded.payload_path)
            relocation_authorization = release_signing.verify_publish_authorization(
                signing_request_path=moved_request,
                signature_path=moved / (index_path.name + ".sig.json"),
                authorization_path=moved
                / (request_path.name + ".authorization.sig.json"),
                asset_list_path=moved / "release-assets.txt",
                trust_policy_path=frozen_policy,
                expected_repo=REPO,
                expected_channel=CHANNEL,
                expected_version="1.2.3",
                expected_tag="v1.2.3",
                expected_commit_sha="a" * 40,
                expected_release_sequence=release_sequence,
            )
            self.assertIn(moved_request, relocation_authorization.authorized_asset_paths)
            original = json.loads(moved_request.read_text(encoding="utf-8"))

            def write_request(value: object) -> None:
                moved_request.write_bytes(
                    (json.dumps(value, indent=2, sort_keys=True) + "\n").encode(
                        "utf-8"
                    )
                )

            traversal = json.loads(json.dumps(original))
            traversal["release_assets"][0]["path"] = "../outside.bin"
            write_request(traversal)
            with self.assertRaisesRegex(ReleaseSigningError, "traversal"):
                release_signing.load_offline_signing_request(moved_request)

            collision = json.loads(json.dumps(original))
            duplicate = dict(collision["release_assets"][0])
            duplicate["path"] = duplicate["path"].swapcase()
            collision["release_assets"].append(duplicate)
            write_request(collision)
            with self.assertRaisesRegex(ReleaseSigningError, "repeats a release asset"):
                release_signing.load_offline_signing_request(moved_request)

            policy_path = moved / "trust-policy.json"
            hardlink_path = moved / "hardlink-policy.json"
            os.link(policy_path, hardlink_path)
            hardlink = json.loads(json.dumps(original))
            hardlink["trust_policy"]["path"] = hardlink_path.name
            write_request(hardlink)
            with self.assertRaisesRegex(ReleaseSigningError, "hard links"):
                release_signing.load_offline_signing_request(moved_request)
            hardlink_path.unlink()

            write_request(original)
            symlink_path = moved / "linked-policy.json"
            try:
                symlink_path.symlink_to(policy_path.name)
            except OSError:
                pass
            else:
                linked = json.loads(json.dumps(original))
                linked["trust_policy"]["path"] = symlink_path.name
                write_request(linked)
                with self.assertRaisesRegex(ReleaseSigningError, "non-link"):
                    release_signing.load_offline_signing_request(moved_request)

    def test_duplicate_signature_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            key = self.materialize_key(root)
            policy = self.materialize_policy(root)
            payload = release_index_payload()
            envelope = create_signature_envelope(payload, "index.json", [key])
            envelope["signatures"].append(dict(envelope["signatures"][0]))

            with self.assertRaisesRegex(ReleaseSigningError, "repeats key"):
                verify_signature_envelope(
                    payload,
                    "index.json",
                    serialize_signature_envelope(envelope),
                    policy,
                )

    def test_index_and_request_authorizations_are_cross_domain_incompatible(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            key = self.materialize_key(root)
            policy = self.materialize_policy(root)
            index_payload = release_index_payload()
            request_payload = b'{"schema_version":2,"status":"awaiting-signatures"}\n'
            index_envelope = create_signature_envelope(
                index_payload,
                "index.json",
                [key],
            )
            request_envelope = release_signing.create_request_authorization_envelope(
                request_payload,
                "request.json",
                [key],
            )

            relabelled_index_signature = {
                "schema_version": release_signing.REQUEST_AUTHORIZATION_SCHEMA_VERSION,
                "request": release_signing._request_authorization_descriptor(
                    request_payload,
                    "request.json",
                ),
                "authorizations": index_envelope["signatures"],
            }
            with self.assertRaisesRegex(ReleaseSigningError, "threshold not met"):
                release_signing.verify_request_authorization_envelope(
                    request_payload,
                    "request.json",
                    release_signing.serialize_request_authorization_envelope(
                        relabelled_index_signature
                    ),
                    policy,
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                )

            relabelled_request_authorization = {
                "schema_version": release_signing.SIGNATURE_SCHEMA_VERSION,
                "payload": release_signing._signature_payload_descriptor(
                    index_payload,
                    "index.json",
                ),
                "signatures": request_envelope["authorizations"],
            }
            with self.assertRaisesRegex(ReleaseSigningError, "threshold not met"):
                verify_signature_envelope(
                    index_payload,
                    "index.json",
                    serialize_signature_envelope(relabelled_request_authorization),
                    policy,
                    expected_repo=REPO,
                    expected_channel=CHANNEL,
                )

    def test_bounded_policy_signature_and_filename_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            key = self.materialize_key(root)
            policy = self.materialize_policy(root)
            payload = release_index_payload()

            with self.assertRaisesRegex(ReleaseSigningError, "at most 255"):
                create_signature_envelope(payload, "a" * 256, [key])

            excessive_keys = [
                PrivateSigningKey(
                    f"{index:064x}", key.public_key, key.private_seed
                )
                for index in range(MAX_SIGNATURES + 1)
            ]
            with self.assertRaisesRegex(ReleaseSigningError, "at most 64"):
                create_signature_envelope(payload, "index.json", excessive_keys)

            policy_document = {
                "schema_version": 1,
                "threshold": 1,
                "keys": [
                    trust_key_document(
                        self.key_document, repos=[REPO], channels=[CHANNEL]
                    )
                ]
                * (MAX_POLICY_KEYS + 1),
            }
            policy_path = root / "oversized-policy.json"
            self.write_json(policy_path, policy_document)
            with self.assertRaisesRegex(ReleaseSigningError, "1..32"):
                load_trust_policy(policy_path)

            scoped_document = trust_key_document(
                self.key_document,
                repos=[f"example/repo-{index}" for index in range(MAX_SCOPES_PER_KEY + 1)],
                channels=[CHANNEL],
            )
            policy_document["keys"] = [scoped_document]
            scoped_path = root / "oversized-scopes.json"
            self.write_json(scoped_path, policy_document)
            with self.assertRaisesRegex(ReleaseSigningError, "1..64"):
                load_trust_policy(scoped_path)

            envelope = create_signature_envelope(payload, "index.json", [key])
            envelope["signatures"] = [
                {
                    **envelope["signatures"][0],
                    "key_id": f"{index:064x}",
                }
                for index in range(MAX_SIGNATURES + 1)
            ]
            with self.assertRaisesRegex(ReleaseSigningError, "1..64"):
                verify_signature_envelope(
                    payload,
                    "index.json",
                    serialize_signature_envelope(envelope),
                    policy,
                )

            valid_envelope = serialize_signature_envelope(
                create_signature_envelope(payload, "index.json", [key])
            )
            oversized_envelope = valid_envelope + b" " * (
                MAX_SIGNATURE_ENVELOPE_SIZE - len(valid_envelope) + 1
            )
            with self.assertRaisesRegex(ReleaseSigningError, "between 1"):
                verify_signature_envelope(
                    payload, "index.json", oversized_envelope, policy
                )

    def test_non_finite_json_numbers_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            policy = self.materialize_policy(pathlib.Path(temp))
            with self.assertRaisesRegex(ReleaseSigningError, "non-finite"):
                verify_signature_envelope(
                    release_index_payload(),
                    "index.json",
                    b'{"schema_version":NaN,"payload":{},"signatures":[]}',
                    policy,
                )

    def test_publish_ready_verification_requires_native_freshness_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            key = self.materialize_key(pathlib.Path(temp))
            policy = self.materialize_policy(pathlib.Path(temp))
            payload = json.dumps({"repo": REPO, "channel": CHANNEL}).encode()
            envelope = create_signature_envelope(payload, "index.json", [key])
            with self.assertRaisesRegex(ReleaseSigningError, "schema_version"):
                verify_signature_envelope(
                    payload,
                    "index.json",
                    serialize_signature_envelope(envelope),
                    policy,
                )

    def test_private_seed_must_match_public_identity(self) -> None:
        different_document = generate_private_key_document()
        corrupted = dict(self.key_document)
        corrupted["private_key"] = different_document["private_key"]
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "corrupted.json"
            self.write_json(path, corrupted, private=True)
            with self.assertRaisesRegex(ReleaseSigningError, "does not match"):
                load_private_signing_key(path)

    @unittest.skipIf(os.name == "nt", "symbolic-link creation is not portable on Windows")
    def test_direct_sign_cli_does_not_resolve_symbolic_key_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            target = root / "target-key.json"
            self.write_json(target, self.key_document, private=True)
            link = root / "linked-key.json"
            link.symlink_to(target)
            payload_path = root / "index.json"
            payload_path.write_bytes(release_index_payload())
            signature_path = root / "index.sig.json"
            argv = [
                "release_signing.py",
                "sign",
                "--payload",
                str(payload_path),
                "--signature",
                str(signature_path),
                "--signing-key-file",
                str(link),
            ]

            stderr = io.StringIO()
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                sys,
                "stderr",
                stderr,
            ):
                self.assertEqual(1, release_signing.main())

            self.assertIn("regular non-link", stderr.getvalue())
            self.assertFalse(signature_path.exists())

    def test_direct_sign_cli_never_overwrites_payload_or_private_key(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            key_path = root / "key.json"
            self.write_json(key_path, self.key_document, private=True)
            payload_path = root / "index.json"
            payload_path.write_bytes(release_index_payload())
            original_payload = payload_path.read_bytes()
            original_key = key_path.read_bytes()

            for output_path in (payload_path, key_path):
                argv = [
                    "release_signing.py",
                    "sign",
                    "--payload",
                    str(payload_path),
                    "--signature",
                    str(output_path),
                    "--signing-key-file",
                    str(key_path),
                ]
                with mock.patch.object(sys, "argv", argv), mock.patch.object(
                    sys, "stderr", io.StringIO()
                ):
                    self.assertEqual(1, release_signing.main())

            self.assertEqual(original_payload, payload_path.read_bytes())
            self.assertEqual(original_key, key_path.read_bytes())

    def test_generate_key_cli_rejects_colliding_outputs_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output = pathlib.Path(temp) / "identity.json"
            argv = [
                "release_signing.py",
                "generate-key",
                "--private-key-path",
                str(output),
                "--trust-policy-path",
                str(output),
                "--repo",
                REPO,
                "--channel",
                CHANNEL,
            ]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                sys, "stderr", io.StringIO()
            ):
                self.assertEqual(1, release_signing.main())
            self.assertFalse(output.exists())

    def test_duplicate_json_fields_are_rejected(self) -> None:
        payload = release_index_payload()
        duplicate_envelope = (
            b'{"schema_version":1,"schema_version":1,"payload":{},"signatures":[]}'
        )
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            policy = self.materialize_policy(root)
            with self.assertRaisesRegex(ReleaseSigningError, "duplicate JSON key"):
                verify_signature_envelope(
                    payload, "index.json", duplicate_envelope, policy
                )

    def test_composer_writes_and_lists_verified_signature_asset(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            bundle = root / "bundle"
            bundle.mkdir()
            key_path = root / "key.json"
            self.write_json(key_path, self.key_document, private=True)
            policy_path = root / "trust-policy.json"
            self.write_json(
                policy_path,
                {
                    "schema_version": 1,
                    "threshold": 1,
                    "keys": [
                        trust_key_document(
                            self.key_document, repos=[REPO], channels=[CHANNEL]
                        )
                    ],
                },
            )
            metadata = {
                "repo": REPO,
                "channel": CHANNEL,
                "version": "1.2.3",
                "commit_sha": "a" * 40,
                "platform_id": "test-platform",
                "platform_stub": "test",
                "os": "test",
                "arch": "test",
                "archive_format": "zip",
                "autoupdater": {},
                "roles": {},
                "artifacts": [],
            }
            self.write_json(bundle / "metadata-test-platform.json", metadata)
            index_path = root / "worr-release-index-stable.json"
            asset_list_path = root / "assets.txt"
            argv = [
                "compose_release_index.py",
                "--artifacts-root",
                str(root),
                "--repo",
                REPO,
                "--channel",
                CHANNEL,
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
                "--signing-key-file",
                str(key_path),
                "--trust-policy",
                str(policy_path),
            ]

            with mock.patch.object(sys, "argv", argv):
                self.assertEqual(compose_release_index.main(), 0)

            signature_path = pathlib.Path(f"{index_path}.sig.json")
            self.assertTrue(signature_path.is_file())
            asset_lines = asset_list_path.read_text(encoding="utf-8").splitlines()
            self.assertTrue(any(line.endswith(index_path.name) for line in asset_lines))
            self.assertTrue(any(line.endswith(signature_path.name) for line in asset_lines))

    def test_unsigned_composer_mode_is_development_only(self) -> None:
        base_argv = [
            "compose_release_index.py",
            "--artifacts-root",
            ".",
            "--repo",
            REPO,
            "--channel",
            CHANNEL,
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
            "--allow-unsigned-development-index",
        ]
        with mock.patch.object(sys, "argv", base_argv), self.assertRaisesRegex(
            SystemExit, "restricted"
        ):
            compose_release_index.main()


if __name__ == "__main__":
    unittest.main()
