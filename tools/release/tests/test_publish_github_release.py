from __future__ import annotations

import hashlib
import json
import datetime as dt
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

from tools.release import compose_release_index, publish_github_release, release_signing
from tools.release.publish_github_release import (
    load_local_assets,
    validate_release_document,
    validate_remote_assets,
)
from tools.release.release_signing import (
    AuthorizedPublishAsset,
    ReleaseSigningError,
    generate_private_key_document,
    trust_key_document,
)


class PublishGithubReleaseTests(unittest.TestCase):
    @staticmethod
    def _write_json(
        path: pathlib.Path,
        value: object,
        *,
        private: bool = False,
    ) -> None:
        path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
        if private and os.name != "nt":
            path.chmod(0o600)

    def test_assets_are_hashed_before_final_authorization_and_no_github_call_on_change(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            body = root / "notes.md"
            body.write_text("notes\n", encoding="utf-8")
            events: list[str] = []

            def load_assets(*_args, **_kwargs):
                events.append("hash-assets")
                return {}

            def reject_changed_assets(**_kwargs):
                events.append("verify-authorization")
                raise ReleaseSigningError("release asset bytes changed")

            argv = [
                "publish_github_release.py",
                "--repo",
                "example/WORR",
                "--tag",
                "v1.2.3",
                "--name",
                "v1.2.3",
                "--expected-commit",
                "a" * 40,
                "--expected-version",
                "1.2.3",
                "--expected-release-sequence",
                "1",
                "--body-path",
                str(body),
                "--asset-list-path",
                str(root / "assets.txt"),
                "--signing-request",
                str(root / "request.json"),
                "--signature",
                str(root / "index.sig.json"),
                "--authorization",
                str(root / "request.authorization.sig.json"),
                "--trust-policy",
                str(root / "policy.json"),
                "--channel",
                "stable",
            ]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                publish_github_release,
                "load_local_assets",
                side_effect=load_assets,
            ), mock.patch.object(
                publish_github_release,
                "verify_publish_authorization",
                side_effect=reject_changed_assets,
            ), mock.patch.object(
                publish_github_release,
                "run_gh",
            ) as run_gh_mock, mock.patch.object(
                publish_github_release.subprocess,
                "run",
            ) as subprocess_mock:
                with self.assertRaisesRegex(SystemExit, "asset bytes changed"):
                    publish_github_release.main()

            self.assertEqual(["hash-assets", "verify-authorization"], events)
            run_gh_mock.assert_not_called()
            subprocess_mock.assert_not_called()

    def test_asset_list_swap_between_cache_and_verification_never_reaches_github(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            body = root / "notes.md"
            body.write_text("notes\n", encoding="utf-8")
            cached_path = root / "cached-extra.zip"
            cached_path.write_bytes(b"cached A")
            authorized_path = root / "authorized.zip"
            authorized_path.write_bytes(b"canonical B")
            cached = {
                cached_path.name: publish_github_release.LocalAsset(
                    path=cached_path.resolve(),
                    name=cached_path.name,
                    size=cached_path.stat().st_size,
                    digest=hashlib.sha256(cached_path.read_bytes()).hexdigest(),
                )
            }
            authorization = mock.Mock(
                authorized_assets=(
                    AuthorizedPublishAsset(
                        path=authorized_path.resolve(),
                        name=authorized_path.name,
                        size=authorized_path.stat().st_size,
                        digest=hashlib.sha256(authorized_path.read_bytes()).hexdigest(),
                    ),
                )
            )
            argv = [
                "publish_github_release.py",
                "--repo",
                "example/WORR",
                "--tag",
                "v1.2.3",
                "--name",
                "v1.2.3",
                "--expected-commit",
                "a" * 40,
                "--expected-version",
                "1.2.3",
                "--expected-release-sequence",
                "1",
                "--body-path",
                str(body),
                "--asset-list-path",
                str(root / "assets.txt"),
                "--signing-request",
                str(root / "request.json"),
                "--signature",
                str(root / "index.sig.json"),
                "--authorization",
                str(root / "request.authorization.sig.json"),
                "--trust-policy",
                str(root / "policy.json"),
                "--channel",
                "stable",
            ]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                publish_github_release,
                "load_local_assets",
                return_value=cached,
            ), mock.patch.object(
                publish_github_release,
                "verify_publish_authorization",
                return_value=authorization,
            ), mock.patch.object(
                publish_github_release,
                "run_gh",
            ) as run_gh_mock, mock.patch.object(
                publish_github_release.subprocess,
                "run",
            ) as subprocess_mock, mock.patch.dict(
                os.environ,
                {"GH_TOKEN": "test-token"},
            ):
                with self.assertRaisesRegex(SystemExit, "cached release assets differ"):
                    publish_github_release.main()
            run_gh_mock.assert_not_called()
            subprocess_mock.assert_not_called()

    def test_signed_channel_publication_semantics_fail_before_github(self) -> None:
        publish_github_release.validate_channel_publication_semantics(
            "stable",
            prerelease=False,
            make_latest=True,
        )
        publish_github_release.validate_channel_publication_semantics(
            "nightly",
            prerelease=True,
            make_latest=False,
        )
        cases = (
            ("stable", ["--prerelease", "--make-latest"]),
            ("stable", []),
            ("nightly", ["--prerelease", "--make-latest"]),
            ("nightly", []),
        )
        for channel, flags in cases:
            with self.subTest(channel=channel, flags=flags), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                body = root / "notes.md"
                body.write_text("notes\n", encoding="utf-8")
                authorization = mock.Mock(
                    authorized_assets=(),
                    verified_index=mock.Mock(index={"channel": channel}),
                )
                argv = [
                    "publish_github_release.py",
                    "--repo",
                    "example/WORR",
                    "--tag",
                    "v1.2.3",
                    "--name",
                    "v1.2.3",
                    "--expected-commit",
                    "a" * 40,
                    "--expected-version",
                    "1.2.3",
                    "--expected-release-sequence",
                    "1",
                    "--body-path",
                    str(body),
                    "--asset-list-path",
                    str(root / "assets.txt"),
                    "--signing-request",
                    str(root / "request.json"),
                    "--signature",
                    str(root / "index.sig.json"),
                    "--authorization",
                    str(root / "request.authorization.sig.json"),
                    "--trust-policy",
                    str(root / "policy.json"),
                    "--channel",
                    channel,
                    *flags,
                ]
                with mock.patch.object(sys, "argv", argv), mock.patch.object(
                    publish_github_release,
                    "load_local_assets",
                    return_value={},
                ), mock.patch.object(
                    publish_github_release,
                    "verify_publish_authorization",
                    return_value=authorization,
                ) as verify_mock, mock.patch.object(
                    publish_github_release,
                    "run_gh",
                ) as run_gh_mock, mock.patch.object(
                    publish_github_release.subprocess,
                    "run",
                ) as subprocess_mock, mock.patch.dict(
                    os.environ,
                    {"GH_TOKEN": "test-token"},
                ):
                    with self.assertRaisesRegex(SystemExit, f"signed {channel} channel"):
                        publish_github_release.main()
                verify_mock.assert_called_once()
                run_gh_mock.assert_not_called()
                subprocess_mock.assert_not_called()

    def test_whole_bundle_attacker_policy_and_key_substitution_never_reaches_github(
        self,
    ) -> None:
        try:
            authoritative_key = generate_private_key_document()
            attacker_key = generate_private_key_document()
        except ReleaseSigningError as exc:
            self.skipTest(str(exc))

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            candidate = root / "release-assets"
            candidate.mkdir()
            self._write_json(
                candidate / "metadata-test.json",
                {
                    "repo": "example/WORR",
                    "channel": "stable",
                    "version": "1.2.3",
                    "commit_sha": "a" * 40,
                    "platform_id": "test",
                    "platform_stub": "test",
                    "os": "test",
                    "arch": "test",
                    "archive_format": "zip",
                    "autoupdater": {},
                    "roles": {},
                    "artifacts": [],
                },
            )
            authoritative_policy_path = root / "frozen-policy.json"
            attacker_policy_path = candidate / "release-trust-policy.json"
            self._write_json(
                authoritative_policy_path,
                {
                    "schema_version": 1,
                    "threshold": 1,
                    "keys": [
                        trust_key_document(
                            authoritative_key,
                            repos=["example/WORR"],
                            channels=["stable"],
                        )
                    ],
                },
            )
            self._write_json(
                attacker_policy_path,
                {
                    "schema_version": 1,
                    "threshold": 1,
                    "keys": [
                        trust_key_document(
                            attacker_key,
                            repos=["example/WORR"],
                            channels=["stable"],
                        )
                    ],
                },
            )
            attacker_key_path = root / "attacker-key.json"
            self._write_json(attacker_key_path, attacker_key, private=True)
            index_path = candidate / "worr-release-index-stable.json"
            request_path = candidate / "release-signing-request.json"
            asset_list_path = candidate / "release-assets.txt"
            compose_argv = [
                "compose_release_index.py",
                "--artifacts-root",
                str(candidate),
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
                "--trust-policy",
                str(attacker_policy_path),
                "--defer-signing",
                "--signing-request-path",
                str(request_path),
            ]
            with mock.patch.object(sys, "argv", compose_argv):
                self.assertEqual(0, compose_release_index.main())

            release_sequence = json.loads(index_path.read_text(encoding="utf-8"))[
                "release_sequence"
            ]
            revocation_path = root / "trusted-revocations.json"
            self._write_json(
                revocation_path,
                {"schema_version": 1, "channels": {"stable": []}},
            )
            candidate_args = [
                "--artifacts-root",
                str(candidate),
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
                "--release-sequence",
                str(release_sequence),
                "--valid-for-days",
                "180",
                "--revocation-file",
                str(revocation_path),
            ]

            partial_path = root / "attacker-partial.json"
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
                    str(attacker_key_path),
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

            notes = root / "notes.md"
            notes.write_text("attacker release\n", encoding="utf-8")
            publisher_argv = [
                "publish_github_release.py",
                "--repo",
                "example/WORR",
                "--tag",
                "v1.2.3",
                "--name",
                "v1.2.3",
                "--expected-commit",
                "a" * 40,
                "--expected-version",
                "1.2.3",
                "--expected-release-sequence",
                str(release_sequence),
                "--body-path",
                str(notes),
                "--asset-list-path",
                str(asset_list_path),
                "--signing-request",
                str(request_path),
                "--signature",
                str(index_path) + ".sig.json",
                "--authorization",
                str(request_path) + ".authorization.sig.json",
                "--trust-policy",
                str(authoritative_policy_path),
                "--channel",
                "stable",
            ]
            generated_at = dt.datetime.strptime(
                json.loads(index_path.read_text(encoding="utf-8"))["generated_at_utc"],
                "%Y-%m-%dT%H:%M:%SZ",
            ).replace(tzinfo=dt.timezone.utc)
            authoritative_policy_payload = authoritative_policy_path.read_bytes()
            authoritative_policy_path.write_bytes(attacker_policy_path.read_bytes())
            with mock.patch.object(sys, "argv", publisher_argv), mock.patch.object(
                release_signing,
                "_utc_now",
                return_value=generated_at + dt.timedelta(hours=24, seconds=1),
            ), mock.patch.object(
                publish_github_release,
                "run_gh",
            ) as run_gh_mock, mock.patch.object(
                publish_github_release.subprocess,
                "run",
            ) as subprocess_mock:
                with self.assertRaisesRegex(SystemExit, "authorization window"):
                    publish_github_release.main()
            run_gh_mock.assert_not_called()
            subprocess_mock.assert_not_called()
            authoritative_policy_path.write_bytes(authoritative_policy_payload)

            with mock.patch.object(sys, "argv", publisher_argv), mock.patch.object(
                publish_github_release,
                "run_gh",
            ) as run_gh_mock, mock.patch.object(
                publish_github_release.subprocess,
                "run",
            ) as subprocess_mock:
                with self.assertRaisesRegex(SystemExit, "independently frozen policy"):
                    publish_github_release.main()
            run_gh_mock.assert_not_called()
            subprocess_mock.assert_not_called()

    def test_final_get_metadata_mismatch_fails_after_patch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            body_path = root / "notes.md"
            body_path.write_text("release body\n", encoding="utf-8")
            base_document = {
                "id": 7,
                "tag_name": "v1.2.3",
                "target_commitish": "a" * 40,
                "name": "v1.2.3",
                "body": "release body\n",
                "prerelease": False,
            }
            tag_reference = {
                "ref": "refs/tags/v1.2.3",
                "object": {"type": "commit", "sha": "a" * 40},
            }
            responses = [
                tag_reference,
                {**base_document, "draft": True},
                tag_reference,
                [[]],
                [[]],
                {**base_document, "draft": False},
                {**base_document, "draft": False, "name": "wrong name"},
            ]
            argv = [
                "publish_github_release.py",
                "--repo",
                "example/WORR",
                "--tag",
                "v1.2.3",
                "--name",
                "v1.2.3",
                "--expected-commit",
                "a" * 40,
                "--expected-version",
                "1.2.3",
                "--expected-release-sequence",
                "1",
                "--body-path",
                str(body_path),
                "--asset-list-path",
                str(root / "assets.txt"),
                "--signing-request",
                str(root / "request.json"),
                "--signature",
                str(root / "index.sig.json"),
                "--authorization",
                str(root / "request.authorization.sig.json"),
                "--trust-policy",
                str(root / "frozen-policy.json"),
                "--channel",
                "stable",
                "--make-latest",
            ]
            upload_result = mock.Mock(returncode=0, stderr="")
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                publish_github_release,
                "load_local_assets",
                return_value={},
            ), mock.patch.object(
                publish_github_release,
                "verify_publish_authorization",
                return_value=mock.Mock(
                    authorized_assets=(),
                    verified_index=mock.Mock(index={"channel": "stable"}),
                ),
            ), mock.patch.object(
                publish_github_release,
                "run_gh",
                side_effect=responses,
            ) as run_gh_mock, mock.patch.object(
                publish_github_release.subprocess,
                "run",
                return_value=upload_result,
            ), mock.patch.dict(os.environ, {"GH_TOKEN": "test-token"}):
                with self.assertRaisesRegex(SystemExit, "metadata differs"):
                    publish_github_release.main()
            self.assertEqual(7, run_gh_mock.call_count)

    def test_existing_tag_mismatch_fails_before_any_draft_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            body = root / "notes.md"
            body.write_text("release body\n", encoding="utf-8")
            argv = [
                "publish_github_release.py",
                "--repo",
                "example/WORR",
                "--tag",
                "v1.2.3",
                "--name",
                "v1.2.3",
                "--expected-commit",
                "a" * 40,
                "--expected-version",
                "1.2.3",
                "--expected-release-sequence",
                "1",
                "--body-path",
                str(body),
                "--asset-list-path",
                str(root / "assets.txt"),
                "--signing-request",
                str(root / "request.json"),
                "--signature",
                str(root / "index.sig.json"),
                "--authorization",
                str(root / "request.authorization.sig.json"),
                "--trust-policy",
                str(root / "policy.json"),
                "--channel",
                "stable",
                "--make-latest",
            ]
            authorization = mock.Mock(
                authorized_assets=(),
                verified_index=mock.Mock(index={"channel": "stable"}),
            )
            wrong_reference = {
                "ref": "refs/tags/v1.2.3",
                "object": {"type": "commit", "sha": "b" * 40},
            }
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                publish_github_release,
                "load_local_assets",
                return_value={},
            ), mock.patch.object(
                publish_github_release,
                "verify_publish_authorization",
                return_value=authorization,
            ), mock.patch.object(
                publish_github_release,
                "run_gh",
                return_value=wrong_reference,
            ) as run_gh_mock, mock.patch.object(
                publish_github_release.subprocess,
                "run",
            ) as subprocess_mock, mock.patch.dict(
                os.environ,
                {"GH_TOKEN": "test-token"},
            ):
                with self.assertRaisesRegex(SystemExit, "tag points to"):
                    publish_github_release.main()
            run_gh_mock.assert_called_once_with(
                ["api", "repos/example/WORR/git/ref/tags/v1.2.3"],
                allow_not_found=True,
            )
            self.assertNotIn("--method", run_gh_mock.call_args.args[0])
            subprocess_mock.assert_not_called()

    def test_existing_public_release_rechecks_tag_before_idempotent_success(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            body = root / "notes.md"
            body.write_text("release body\n", encoding="utf-8")
            release = {
                "id": 7,
                "tag_name": "v1.2.3",
                "target_commitish": "a" * 40,
                "name": "v1.2.3",
                "body": "release body\n",
                "draft": False,
                "prerelease": False,
            }
            correct_reference = {
                "ref": "refs/tags/v1.2.3",
                "object": {"type": "commit", "sha": "a" * 40},
            }
            argv = [
                "publish_github_release.py",
                "--repo",
                "example/WORR",
                "--tag",
                "v1.2.3",
                "--name",
                "v1.2.3",
                "--expected-commit",
                "a" * 40,
                "--expected-version",
                "1.2.3",
                "--expected-release-sequence",
                "1",
                "--body-path",
                str(body),
                "--asset-list-path",
                str(root / "assets.txt"),
                "--signing-request",
                str(root / "request.json"),
                "--signature",
                str(root / "index.sig.json"),
                "--authorization",
                str(root / "request.authorization.sig.json"),
                "--trust-policy",
                str(root / "policy.json"),
                "--channel",
                "stable",
                "--make-latest",
            ]
            authorization = mock.Mock(
                authorized_assets=(),
                verified_index=mock.Mock(index={"channel": "stable"}),
            )
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                publish_github_release,
                "load_local_assets",
                return_value={},
            ), mock.patch.object(
                publish_github_release,
                "verify_publish_authorization",
                return_value=authorization,
            ), mock.patch.object(
                publish_github_release,
                "run_gh",
                side_effect=[correct_reference, release, None],
            ) as run_gh_mock, mock.patch.object(
                publish_github_release.subprocess,
                "run",
            ) as subprocess_mock, mock.patch.dict(
                os.environ,
                {"GH_TOKEN": "test-token"},
            ):
                with self.assertRaisesRegex(SystemExit, "tag is missing"):
                    publish_github_release.main()
            self.assertEqual(3, run_gh_mock.call_count)
            subprocess_mock.assert_not_called()

    def test_post_publish_tag_race_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            body = root / "notes.md"
            body.write_text("release body\n", encoding="utf-8")
            release = {
                "id": 7,
                "tag_name": "v1.2.3",
                "target_commitish": "a" * 40,
                "name": "v1.2.3",
                "body": "release body\n",
                "prerelease": False,
            }
            correct_reference = {
                "ref": "refs/tags/v1.2.3",
                "object": {"type": "commit", "sha": "a" * 40},
            }
            raced_reference = {
                "ref": "refs/tags/v1.2.3",
                "object": {"type": "commit", "sha": "b" * 40},
            }
            responses = [
                correct_reference,
                {**release, "draft": True},
                correct_reference,
                [[]],
                [[]],
                {**release, "draft": False},
                {**release, "draft": False},
                raced_reference,
            ]
            argv = [
                "publish_github_release.py",
                "--repo",
                "example/WORR",
                "--tag",
                "v1.2.3",
                "--name",
                "v1.2.3",
                "--expected-commit",
                "a" * 40,
                "--expected-version",
                "1.2.3",
                "--expected-release-sequence",
                "1",
                "--body-path",
                str(body),
                "--asset-list-path",
                str(root / "assets.txt"),
                "--signing-request",
                str(root / "request.json"),
                "--signature",
                str(root / "index.sig.json"),
                "--authorization",
                str(root / "request.authorization.sig.json"),
                "--trust-policy",
                str(root / "policy.json"),
                "--channel",
                "stable",
                "--make-latest",
            ]
            authorization = mock.Mock(
                authorized_assets=(),
                verified_index=mock.Mock(index={"channel": "stable"}),
            )
            upload_result = mock.Mock(returncode=0, stderr="")
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                publish_github_release,
                "load_local_assets",
                return_value={},
            ), mock.patch.object(
                publish_github_release,
                "verify_publish_authorization",
                return_value=authorization,
            ), mock.patch.object(
                publish_github_release,
                "run_gh",
                side_effect=responses,
            ) as run_gh_mock, mock.patch.object(
                publish_github_release.subprocess,
                "run",
                return_value=upload_result,
            ) as subprocess_mock, mock.patch.dict(
                os.environ,
                {"GH_TOKEN": "test-token"},
            ):
                with self.assertRaisesRegex(SystemExit, "tag points to"):
                    publish_github_release.main()
            self.assertEqual(8, run_gh_mock.call_count)
            subprocess_mock.assert_called_once()

    def test_annotated_tag_resolution_is_typed_and_bounded(self) -> None:
        annotated_sha = "b" * 40
        reference = {
            "ref": "refs/tags/v1.2.3",
            "object": {"type": "tag", "sha": annotated_sha},
        }
        tag_object = {
            "object": {"type": "commit", "sha": "a" * 40},
        }
        with mock.patch.object(
            publish_github_release,
            "run_gh",
            side_effect=[reference, tag_object],
        ) as run_gh_mock:
            self.assertEqual(
                "a" * 40,
                publish_github_release.resolve_tag_commit(
                    "example/WORR",
                    "v1.2.3",
                ),
            )
        self.assertEqual(2, run_gh_mock.call_count)

        chain = [f"{index:040x}" for index in range(1, 10)]
        unbounded_responses = [
            {
                "ref": "refs/tags/v1.2.3",
                "object": {"type": "tag", "sha": chain[0]},
            },
            *[
                {"object": {"type": "tag", "sha": next_sha}}
                for next_sha in chain[1:]
            ],
        ]
        with mock.patch.object(
            publish_github_release,
            "run_gh",
            side_effect=unbounded_responses,
        ) as run_gh_mock:
            with self.assertRaisesRegex(ValueError, "peel limit"):
                publish_github_release.resolve_tag_commit(
                    "example/WORR",
                    "v1.2.3",
                )
        self.assertEqual(
            1 + publish_github_release.MAX_TAG_PEEL_DEPTH,
            run_gh_mock.call_count,
        )

    def test_unsigned_additional_asset_option_is_rejected_before_github(self) -> None:
        argv = [
            "publish_github_release.py",
            "--repo",
            "example/WORR",
            "--tag",
            "v1.2.3",
            "--name",
            "v1.2.3",
            "--expected-commit",
            "a" * 40,
            "--expected-version",
            "1.2.3",
            "--expected-release-sequence",
            "1",
            "--body-path",
            "notes.md",
            "--asset-list-path",
            "assets.txt",
            "--signing-request",
            "request.json",
            "--signature",
            "index.sig.json",
            "--authorization",
            "request.authorization.sig.json",
            "--trust-policy",
            "frozen-policy.json",
            "--channel",
            "stable",
            "--additional-asset",
            "unsigned.zip",
        ]
        with mock.patch.object(sys, "argv", argv), mock.patch.object(
            publish_github_release,
            "run_gh",
        ) as run_gh_mock, mock.patch.object(sys, "stderr"):
            with self.assertRaises(SystemExit):
                publish_github_release.main()
        run_gh_mock.assert_not_called()

    def test_local_assets_require_unique_basenames(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = root / "a" / "payload.zip"
            second = root / "b" / "payload.zip"
            first.parent.mkdir()
            second.parent.mkdir()
            first.write_bytes(b"one")
            second.write_bytes(b"two")
            asset_list = root / "assets.txt"
            asset_list.write_text(f"{first}\n{second}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate release asset"):
                load_local_assets(asset_list)

    def test_local_asset_basenames_are_unique_under_portable_comparison(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = root / "a" / "Payload.zip"
            second = root / "b" / "payload.ZIP"
            first.parent.mkdir()
            second.parent.mkdir()
            first.write_bytes(b"one")
            second.write_bytes(b"two")
            asset_list = root / "assets.txt"
            asset_list.write_text(f"{first}\n{second}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "portable comparison"):
                load_local_assets(asset_list)

    def test_release_document_is_bound_to_expected_commit(self) -> None:
        document = {
            "id": 7,
            "tag_name": "v1.0.0",
            "target_commitish": "a" * 40,
            "draft": True,
        }
        self.assertEqual(
            7,
            validate_release_document(
                document,
                tag="v1.0.0",
                expected_commit="a" * 40,
                require_draft=True,
            ),
        )
        with self.assertRaisesRegex(ValueError, "target differs"):
            validate_release_document(
                document,
                tag="v1.0.0",
                expected_commit="b" * 40,
                require_draft=True,
            )

    def test_remote_asset_set_size_and_digest_must_match_exactly(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            payload = root / "payload.zip"
            payload.write_bytes(b"payload")
            asset_list = root / "assets.txt"
            asset_list.write_text(f"{payload}\n", encoding="utf-8")
            expected = load_local_assets(asset_list)
            digest = hashlib.sha256(b"payload").hexdigest()
            validate_remote_assets(
                expected,
                [
                    {
                        "name": "payload.zip",
                        "state": "uploaded",
                        "size": 7,
                        "digest": f"sha256:{digest}",
                    }
                ],
            )
            with self.assertRaisesRegex(ValueError, "wrong SHA-256"):
                validate_remote_assets(
                    expected,
                    [
                        {
                            "name": "payload.zip",
                            "state": "uploaded",
                            "size": 7,
                            "digest": "sha256:" + "0" * 64,
                        }
                    ],
                )

    def test_remote_asset_extras_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            payload = root / "payload.zip"
            payload.write_bytes(b"payload")
            asset_list = root / "assets.txt"
            asset_list.write_text(f"{payload}\n", encoding="utf-8")
            expected = load_local_assets(asset_list)
            with self.assertRaisesRegex(ValueError, "unexpected stale.zip"):
                validate_remote_assets(
                    expected,
                    [
                        {
                            "name": "payload.zip",
                            "state": "uploaded",
                            "size": 7,
                            "digest": "sha256:" + hashlib.sha256(b"payload").hexdigest(),
                        },
                        {
                            "name": "stale.zip",
                            "state": "uploaded",
                            "size": 1,
                            "digest": "sha256:" + "0" * 64,
                        },
                    ],
                )


if __name__ == "__main__":
    unittest.main()
