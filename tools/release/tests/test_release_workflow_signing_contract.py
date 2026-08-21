from __future__ import annotations

import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
CHECKOUT_PIN = "actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd"
UPLOAD_PIN = "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"
DOWNLOAD_PIN = "actions/download-artifact@37930b1c2abaa49bbe596cd826c3c89aef350131"


def job_block(text: str, job_name: str) -> str:
    marker = f"\n  {job_name}:\n"
    try:
        start = text.index(marker) + 1
    except ValueError as exc:
        raise AssertionError(f"workflow does not define job {job_name!r}") from exc
    next_job = text.find("\n  ", start + len(marker))
    while next_job >= 0:
        line_end = text.find("\n", next_job + 1)
        candidate = text[next_job + 1 : line_end if line_end >= 0 else None]
        if (
            candidate.startswith("  ")
            and not candidate.startswith("    ")
            and candidate.endswith(":")
        ):
            return text[start:next_job]
        next_job = text.find("\n  ", next_job + 3)
    return text[start:]


class ReleaseWorkflowSigningContractTests(unittest.TestCase):
    def test_production_workflows_separate_candidate_authorization_and_publish(self) -> None:
        contracts = (
            (
                "release.yml",
                "stable",
                "WORR_STABLE_RELEASE",
                "release-*",
                "worr-release-index-stable.json",
                "stable-release-signing",
                "stable-release-publishing",
                "180",
            ),
            (
                "nightly.yml",
                "nightly",
                "WORR_NIGHTLY_RELEASE",
                "nightly-*",
                "worr-release-index-nightly.json",
                "nightly-release-signing",
                "nightly-release-publishing",
                "14",
            ),
        )
        for (
            filename,
            channel,
            secret_prefix,
            artifact_pattern,
            index_name,
            signing_environment,
            publishing_environment,
            validity_days,
        ) in contracts:
            with self.subTest(workflow=filename):
                text = (REPO_ROOT / ".github" / "workflows" / filename).read_text(
                    encoding="utf-8"
                )
                candidate = job_block(text, "release_candidate")
                authorize = job_block(text, "authorize_release")
                publish = job_block(text, "publish_release")
                prepare = job_block(text, "prepare")
                build = job_block(text, "build")

                self.assertIn(
                    "release_sequence: ${{ steps.sequence.outputs.value }}",
                    prepare,
                )
                self.assertIn("Compute monotonic release sequence", prepare)
                self.assertIn(
                    "trust_policy_artifact_id: ${{ steps.upload_trust_policy.outputs.artifact-id }}",
                    prepare,
                )
                self.assertIn(
                    "trust_policy_artifact_digest: ${{ steps.upload_trust_policy.outputs.artifact-digest }}",
                    prepare,
                )
                trust_policy_upload = prepare[
                    prepare.index(f"Upload immutable {channel} release trust policy") :
                    prepare.index("Remove materialized", prepare.index(f"Upload immutable {channel} release trust policy"))
                ]
                self.assertIn("id: upload_trust_policy", trust_policy_upload)
                self.assertIn(
                    f"name: worr-{channel}-trust-policy-${{{{ github.run_id }}}}-${{{{ github.run_attempt }}}}",
                    trust_policy_upload,
                )
                self.assertIn("overwrite: false", trust_policy_upload)
                self.assertNotIn("overwrite: true", trust_policy_upload)
                trust_policy_download_binding = (
                    "artifact-ids: ${{ needs.prepare.outputs.trust_policy_artifact_id }}"
                )
                for consumer_name, consumer in (
                    ("build", build),
                    ("release_candidate", candidate),
                    ("authorize_release", authorize),
                    ("publish_release", publish),
                ):
                    with self.subTest(workflow=filename, consumer=consumer_name):
                        self.assertIn(trust_policy_download_binding, consumer)
                        self.assertNotIn(
                            f"name: worr-{channel}-trust-policy-", consumer
                        )
                self.assertIn("needs: [prepare, build]", candidate)
                self.assertNotIn("Compute monotonic release sequence", candidate)
                self.assertNotIn("steps.sequence.outputs.value", candidate)
                self.assertIn("contents: read", candidate)
                self.assertNotIn("contents: write", candidate)
                self.assertNotIn("environment: stable-release-signing", candidate)
                self.assertNotIn("environment: nightly-release-signing", candidate)
                self.assertNotIn(f"{secret_prefix}_SIGNING_KEY_B64", candidate)
                self.assertNotIn("WORR_RELEASE_SIGNING_KEY_LIST_PATH", candidate)
                self.assertNotIn("materialize_signing_secrets.py", candidate)
                self.assertIn("Compose unsigned release candidate", candidate)
                self.assertLess(
                    candidate.index("Verify expected artifacts"),
                    candidate.index("Compose unsigned release candidate"),
                )
                self.assertIn("--defer-signing", candidate)
                self.assertIn(
                    "--signing-request-path release-assets/release-signing-request.json",
                    candidate,
                )
                self.assertIn(
                    f"--index-path release-assets/{index_name}", candidate
                )
                self.assertIn(
                    "--trust-policy release-assets/release-trust-policy.json",
                    candidate,
                )
                self.assertIn(
                    '--release-sequence "${{ needs.prepare.outputs.release_sequence }}"',
                    candidate,
                )
                self.assertIn(
                    f"worr-{channel}-release-candidate-${{{{ github.run_id }}}}",
                    candidate,
                )
                candidate_upload = candidate[candidate.index("Upload immutable") :]
                self.assertNotIn("release-assets/release-assets.txt", candidate_upload)
                self.assertNotIn(".sig.json", candidate_upload)
                self.assertNotIn("publish_github_release.py", candidate)
                self.assertNotIn("GH_TOKEN", candidate)

                self.assertIn(
                    "needs: [prepare, build, release_candidate]", authorize
                )
                self.assertIn(f"environment: {signing_environment}", authorize)
                self.assertIn("contents: read", authorize)
                self.assertNotIn("contents: write", authorize)
                self.assertIn("persist-credentials: false", authorize)
                self.assertIn("Download immutable", authorize)
                self.assertIn("release candidate control", authorize)
                self.assertIn(trust_policy_download_binding, authorize)
                self.assertIn("Download build artifacts for authorization", authorize)
                self.assertIn(f"pattern: {artifact_pattern}", authorize)
                self.assertIn("cmp --silent --", authorize)
                self.assertIn(
                    "release-policy/release-trust-policy.json", authorize
                )
                self.assertIn(
                    "release-assets/release-trust-policy.json", authorize
                )
                self.assertIn(f"{secret_prefix}_SIGNING_KEY_B64", authorize)
                self.assertIn("materialize_signing_secrets.py", authorize)
                self.assertIn(
                    "--trust-policy-file release-policy/release-trust-policy.json",
                    authorize,
                )
                self.assertIn("release_signing.py sign-request", authorize)
                self.assertIn("release_signing.py verify-candidate", authorize)
                self.assertIn("release_signing.py aggregate", authorize)
                self.assertNotIn("publish_github_release.py", authorize)
                self.assertNotIn("GH_TOKEN", authorize)
                self.assertIn(
                    '--signing-key-list "${WORR_RELEASE_SIGNING_KEY_LIST_PATH}"',
                    authorize,
                )
                self.assertIn("if: always()", authorize)
                self.assertLess(
                    authorize.index("cmp --silent --"),
                    authorize.index("release_signing.py verify-candidate"),
                )
                self.assertLess(
                    authorize.index("release_signing.py verify-candidate"),
                    authorize.index("materialize_signing_secrets.py"),
                )
                self.assertLess(
                    authorize.index("materialize_signing_secrets.py"),
                    authorize.index("release_signing.py sign-request"),
                )
                self.assertLess(
                    authorize.index("release_signing.py aggregate"),
                    authorize.index("cleanup_signing_secrets.py"),
                )
                self.assertLess(
                    authorize.index("cleanup_signing_secrets.py"),
                    authorize.index("Upload immutable"),
                )
                self.assertIn(
                    "release-assets/release-signing-request.json.authorization.sig.json",
                    authorize[authorize.index("Upload immutable") :],
                )
                verify_candidate = authorize[
                    authorize.index("release_signing.py verify-candidate") :
                    authorize.index("Materialize", authorize.index("release_signing.py verify-candidate"))
                ]
                sign_request = authorize[
                    authorize.index("release_signing.py sign-request") :
                    authorize.index("release_signing.py aggregate")
                ]
                expected_tag = (
                    '--tag "v${{ needs.prepare.outputs.version }}"'
                    if channel == "stable"
                    else '--tag "${{ needs.prepare.outputs.nightly_tag }}"'
                )
                for validation_argument in (
                    "--artifacts-root release-assets",
                    '--repo "${{ github.repository }}"',
                    f"--channel {channel}",
                    '--version "${{ needs.prepare.outputs.version }}"',
                    expected_tag,
                    '--commit-sha "${{ needs.prepare.outputs.head_sha }}"',
                    '--release-sequence "${{ needs.prepare.outputs.release_sequence }}"',
                    f"--valid-for-days {validity_days}",
                    "--revocation-file tools/release/revoked-release-indexes.json",
                ):
                    self.assertIn(validation_argument, verify_candidate)
                    self.assertIn(validation_argument, sign_request)

                self.assertIn(
                    "needs: [prepare, build, release_candidate, authorize_release]",
                    publish,
                )
                self.assertIn(f"environment: {publishing_environment}", publish)
                self.assertIn("contents: write", publish)
                self.assertIn("persist-credentials: false", publish)
                self.assertIn(trust_policy_download_binding, publish)
                self.assertNotIn(f"{secret_prefix}_SIGNING_KEY_B64", publish)
                self.assertNotIn("secrets.", publish)
                self.assertNotIn("WORR_RELEASE_SIGNING_KEY_LIST_PATH", publish)
                self.assertNotIn("materialize_signing_secrets.py", publish)
                self.assertNotIn("cleanup_signing_secrets.py", publish)
                self.assertIn("release_signing.py verify-publish", publish)
                self.assertIn("tools/release/publish_github_release.py", publish)
                self.assertIn("cmp --silent --", publish)
                self.assertLess(
                    publish.index("cmp --silent --"),
                    publish.index("release_signing.py verify-publish"),
                )
                self.assertLess(
                    publish.index("release_signing.py verify-publish"),
                    publish.index("tools/release/publish_github_release.py"),
                )
                verify_publish = publish[
                    publish.index("release_signing.py verify-publish") :
                    publish.index("Generate release notes")
                ]
                publisher = publish[
                    publish.index("tools/release/publish_github_release.py") :
                ]
                self.assertIn(
                    '--version "${{ needs.prepare.outputs.version }}"',
                    verify_publish,
                )
                self.assertIn(
                    '--release-sequence "${{ needs.prepare.outputs.release_sequence }}"',
                    verify_publish,
                )
                self.assertIn(
                    '--expected-version "${{ needs.prepare.outputs.version }}"',
                    publisher,
                )
                self.assertIn(
                    '--expected-release-sequence "${{ needs.prepare.outputs.release_sequence }}"',
                    publisher,
                )
                self.assertNotIn(
                    "needs.release_candidate.outputs.release_sequence",
                    text,
                )
                for required_argument in (
                    "--signing-request release-assets/release-signing-request.json",
                    f"--signature release-assets/{index_name}.sig.json",
                    "--authorization release-assets/release-signing-request.json.authorization.sig.json",
                    "--trust-policy release-policy/release-trust-policy.json",
                    f"--channel {channel}",
                ):
                    self.assertGreaterEqual(publish.count(required_argument), 2)
                self.assertNotIn(
                    "--additional-asset release-assets/release-trust-policy.json",
                    publish,
                )
                self.assertNotIn("--additional-asset", publish)

                self.assertIn("release-signing-requirements.txt", text)
                self.assertIn("--require-hashes", text)
                self.assertIn("--only-binary=:all:", text)
                self.assertIn(
                    f"secrets.{secret_prefix}_TRUST_POLICY_B64", text
                )
                self.assertIn(
                    f"secrets.{secret_prefix}_SIGNING_KEY_B64", text
                )
                self.assertEqual(
                    2,
                    text.count("tools/release/materialize_signing_secrets.py"),
                )
                self.assertNotIn(
                    '--signing-key-file "${WORR_RELEASE_SIGNING_KEY_PATH}"', text
                )
                self.assertIn("tools/release/cleanup_signing_secrets.py", text)
                self.assertIn("if: always()", text)
                self.assertIn("Upload immutable", text)
                self.assertIn("Download immutable", text)
                self.assertIn(f"--channel {channel}", text)
                self.assertNotIn("--allow-unsigned-development", text)
                self.assertIn("tools/release/publish_github_release.py", text)
                self.assertNotIn("softprops/action-gh-release", text)
                self.assertIn("overwrite: true", text)
                self.assertIn("retention-days: 90", text)
                self.assertIn("WORR_BUILD_ID: ${{ github.run_id }}", text)
                self.assertNotIn("WORR_BUILD_ID: ${{ github.run_id }}-${{ github.run_attempt }}", text)
                self.assertIn("git rev-parse \"${RELEASE_TAG}^{commit}\"", text)
                self.assertIn("tools/release/build-tool-requirements.txt", text)
                self.assertIn("sudo apt-get install -y msitools", text)
                self.assertEqual(
                    text.count(CHECKOUT_PIN),
                    text.count("persist-credentials: false"),
                )

    def test_no_release_job_combines_signing_keys_with_write_permission(self) -> None:
        for filename in ("release.yml", "nightly.yml"):
            text = (REPO_ROOT / ".github" / "workflows" / filename).read_text(
                encoding="utf-8"
            )
            for job_name in (
                "prepare",
                "build",
                "release_candidate",
                "authorize_release",
                "publish_release",
            ):
                with self.subTest(workflow=filename, job=job_name):
                    block = job_block(text, job_name)
                    has_private_key = "RELEASE_SIGNING_KEY_B64" in block
                    has_write = "contents: write" in block
                    self.assertFalse(
                        has_private_key and has_write,
                        f"{filename}:{job_name} combines signing keys with publication authority",
                    )

    def test_publish_preserves_release_identity_and_draft_gate_contract(self) -> None:
        stable = job_block(
            (REPO_ROOT / ".github" / "workflows" / "release.yml").read_text(
                encoding="utf-8"
            ),
            "publish_release",
        )
        nightly = job_block(
            (REPO_ROOT / ".github" / "workflows" / "nightly.yml").read_text(
                encoding="utf-8"
            ),
            "publish_release",
        )
        for block in (stable, nightly):
            self.assertIn("Publish exact", block)
            self.assertIn("through a draft gate", block)
            self.assertIn("--expected-commit", block)
            self.assertIn("--asset-list-path release-assets/release-assets.txt", block)
            self.assertNotIn("--additional-asset", block)
        self.assertIn('--tag "v${{ needs.prepare.outputs.version }}"', stable)
        self.assertIn('--make-latest', stable)
        self.assertIn('--tag "${{ needs.prepare.outputs.nightly_tag }}"', nightly)
        self.assertIn('--prerelease', nightly)

    def test_workflow_action_and_runner_contract_is_versioned(self) -> None:
        workflow_paths = tuple((REPO_ROOT / ".github" / "workflows").glob("*.yml"))
        combined = "\n".join(path.read_text(encoding="utf-8") for path in workflow_paths)
        self.assertIn(CHECKOUT_PIN, combined)
        self.assertIn(UPLOAD_PIN, combined)
        self.assertIn(DOWNLOAD_PIN, combined)
        self.assertNotIn("ubuntu-latest", combined.replace("# ubuntu-latest", "# pinned-runner"))
        self.assertNotIn("windows-latest", combined)
        self.assertNotIn("macos-latest", combined)

        targets = (REPO_ROOT / "tools" / "release" / "targets.py").read_text(
            encoding="utf-8"
        )
        for runner in ("windows-2025", "ubuntu-24.04", "macos-15-intel"):
            self.assertIn(runner, targets)

    def test_nightly_identity_is_anchored_to_the_workflow_run(self) -> None:
        text = (REPO_ROOT / ".github" / "workflows" / "nightly.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("actions/runs/${{ github.run_id }}", text)
        self.assertIn("--utc-timestamp \"${RUN_CREATED_AT}\"", text)
        self.assertEqual(3, text.count("--allow-prerelease"))
        self.assertIn("--prerelease", text)

    def test_pull_request_python_dependencies_are_hash_locked(self) -> None:
        text = (REPO_ROOT / ".github" / "workflows" / "pull-request.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("tools/release/release-signing-requirements.txt", text)
        self.assertIn("tools/release/python-test-requirements.txt", text)
        self.assertGreaterEqual(text.count("--require-hashes"), 2)
        self.assertIn("tools/release/verify_revocations_append_only.py", text)
        self.assertIn('git cat-file -e "${BASE_SHA}^{commit}"', text)
        self.assertIn('{"schema_version":1,"channels":{}}', text)

    def test_third_party_actions_are_immutable_commit_pins(self) -> None:
        for path in (REPO_ROOT / ".github" / "workflows").glob("*.yml"):
            with self.subTest(workflow=path.name):
                text = path.read_text(encoding="utf-8")
                for line in text.splitlines():
                    if "uses:" not in line or "./" in line:
                        continue
                    reference = line.split("uses:", 1)[1].strip().split()[0]
                    self.assertRegex(reference, r"^[^@]+@[0-9a-f]{40}$")


if __name__ == "__main__":
    unittest.main()
