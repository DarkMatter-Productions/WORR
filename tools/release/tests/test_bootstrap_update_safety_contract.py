from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
SOURCE = (ROOT / "src" / "updater" / "bootstrap.cpp").read_text(encoding="utf-8")
TRANSACTION_SOURCE = (ROOT / "src" / "updater" / "install_transaction.cpp").read_text(
    encoding="utf-8"
)
INSTALL_ROOT_SOURCE = (ROOT / "src" / "updater" / "install_root.cpp").read_text(
    encoding="utf-8"
)


class BootstrapUpdateSafetyContractTests(unittest.TestCase):
    def test_unsafe_cached_update_is_ignored_instead_of_blocking_launch(self) -> None:
        self.assertIn('BootstrapTrace("Cached pending update rejected: "', SOURCE)
        self.assertIn(
            "LoadPendingUpdate(install_files, config, options, &pending_found,",
            SOURCE,
        )
        self.assertIn("observed_release_revocations, &pending_rejection", SOURCE)
        self.assertIn('warning += " Cached update ignored: " + pending_rejection + ".";', SOURCE)

        parser_start = SOURCE.index("std::optional<RemotePayload> RemotePayloadFromJson")
        manifest_validation = SOURCE.index("ValidateRemotePayloadManifest(payload);", parser_start)
        parser_return = SOURCE.index("return payload;", manifest_validation)
        self.assertLess(manifest_validation, parser_return)
        self.assertIn('throw std::runtime_error("Pending update manifest has no managed files")', SOURCE)
        self.assertIn("Pending update manifest has duplicate file path: ", SOURCE)
        self.assertIn("ValidateRemotePayloadContext(*pending, config, options);", SOURCE)

    def test_cached_update_must_match_the_active_launcher_context(self) -> None:
        context_start = SOURCE.index("void ValidateRemotePayloadContext")
        context_end = SOURCE.index("std::optional<RemotePayload> RemotePayloadFromJson", context_start)
        context = SOURCE[context_start:context_end]
        self.assertIn("payload.role != expected_role", context)
        self.assertIn('JsonString(payload.manifest_json, "repo") != config.repo', context)
        self.assertIn('JsonString(payload.manifest_json, "channel") != config.channel', context)
        self.assertIn("payload.launch_exe != expected_launch", context)
        self.assertIn("payload.engine_library != expected_engine", context)

    def test_zip_member_names_are_read_without_the_fixed_stat_buffer(self) -> None:
        self.assertIn("mz_zip_reader_get_filename(&zip, i, nullptr, 0)", SOURCE)
        self.assertIn("std::vector<char> filename(filename_size);", SOURCE)
        self.assertIn("member.find('\\0')", SOURCE)
        self.assertIn("std::map<std::string, std::string> seen_member_paths;", SOURCE)
        self.assertIn("UpdaterPathCollisionKey(canonical_member)", SOURCE)

    def test_install_writes_are_anchored_for_the_whole_transaction(self) -> None:
        apply_start = SOURCE.index("void ApplyInstallSyncPlan")
        apply_end = SOURCE.index("fs::path BasePathFromExecutable", apply_start)
        apply = SOURCE[apply_start:apply_end]
        self.assertIn("InstallTransactionPlan transaction", apply)
        self.assertIn("ApplyInstallTransaction(stage_dir, install_root, transaction", apply)
        self.assertIn("AnchoredFileTree staged_files", TRANSACTION_SOURCE)
        self.assertIn("AnchoredFileTree installed_files", TRANSACTION_SOURCE)
        self.assertIn('kJournalPath = ".worr-update/journal"', TRANSACTION_SOURCE)
        self.assertIn("RecoverLocked(installed_files, options)", TRANSACTION_SOURCE)
        self.assertIn("installed_files.ReplaceFileFrom(staged_files, file.path", TRANSACTION_SOURCE)
        self.assertIn("installed_files.RemoveFile(path)", TRANSACTION_SOURCE)
        self.assertIn(
            "record_original(plan.manifest_path, manifest_state);",
            TRANSACTION_SOURCE,
        )
        self.assertIn("installed_files.ReplaceFileWithBytes(plan.manifest_path", TRANSACTION_SOURCE)

    def test_runtime_and_updater_share_a_stable_never_replaced_lock(self) -> None:
        launch_start = SOURCE.index("int LaunchEngineAndWait")
        launch_end = SOURCE.index(
            "std::vector<std::string> BuildWorkerInvocation", launch_start
        )
        launch = SOURCE[launch_start:launch_end]
        self.assertIn('kRuntimeCoordinationLockName = "worr_install.lock"', SOURCE)
        self.assertIn("AcquireExistingFileSharedLock(", launch)
        self.assertIn("runtime_lock_expectation", launch)
        self.assertLess(
            launch.index("AcquireExistingFileSharedLock("),
            launch.index("SDL_LoadObject"),
        )
        self.assertIn('kRuntimeLockPath = "worr_install.lock"', TRANSACTION_SOURCE)
        self.assertIn(
            "AcquireExistingFileExclusiveLock(kRuntimeLockPath)",
            TRANSACTION_SOURCE,
        )

    def test_cross_release_file_directory_transitions_reach_the_transaction(self) -> None:
        plan_start = SOURCE.index("InstallSyncPlan BuildInstallSyncPlan")
        plan_end = SOURCE.index("std::string DescribeInstallSyncPlan", plan_start)
        plan = SOURCE[plan_start:plan_end]
        self.assertNotIn("unsupported file-to-directory transition", plan)
        self.assertNotIn("unsupported directory-to-file transition", plan)
        self.assertIn("ValidateRuntimeCoordinationLock", plan)

    def test_interrupted_transaction_is_recovered_before_any_launch_path(self) -> None:
        flow_start = SOURCE.index("int RunBootstrapFlow")
        flow_end = SOURCE.index("BootstrapOptions ParseBootstrapOptions", flow_start)
        flow = SOURCE[flow_start:flow_end]
        recovery = flow.index("RecoverInstallTransaction(options.install_root)")
        skip = flow.index("if (options.skip_update_check)")
        config_load = flow.index("LoadUpdaterConfig(raw_config, options.role)")
        self.assertLess(recovery, skip)
        self.assertLess(recovery, config_load)
        self.assertIn("recovery_options.recover_only = true", flow)

    def test_release_index_is_verified_before_parsing_or_asset_use(self) -> None:
        discovery_start = SOURCE.index("DiscoverRemotePayload(const UpdaterConfig")
        discovery_end = SOURCE.index("void ExtractZip", discovery_start)
        discovery = SOURCE[discovery_start:discovery_end]
        download = discovery.index("const std::string raw_index")
        verification = discovery.index("VerifyReleaseIndexSignature(", download)
        target_selection = discovery.index('index_json["targets"]', verification)
        self.assertLess(download, verification)
        self.assertLess(verification, target_selection)
        self.assertIn('config.release_index_asset + ".sig.json"', discovery)
        self.assertIn("SignedTargetAssetIntegrity(\n          selected_target, manifest_name", discovery)
        self.assertIn("SignedTargetAssetIntegrity(\n          selected_target, package_name", discovery)
        self.assertIn("Update manifest does not match the signed release index", discovery)

    def test_release_discovery_falls_back_across_key_rotation_overlap(self) -> None:
        discovery_start = SOURCE.index("DiscoverRemotePayload(const UpdaterConfig")
        discovery_end = SOURCE.index("void ExtractZip", discovery_start)
        discovery = SOURCE[discovery_start:discovery_end]
        self.assertIn("SelectReleases(releases_json, config)", discovery)
        self.assertIn(
            "for (const Json::Value &release_json : matching_releases)", discovery
        )
        self.assertIn("VerifyReleaseIndexSignature(", discovery)
        self.assertIn("Release candidate rejected: ", discovery)
        self.assertIn("No release candidate is verifiable by this install", discovery)
        self.assertIn("page <= kMaxReleaseFeedPages", discovery)
        self.assertIn('"&page=" + std::to_string(page)', discovery)

    def test_worker_approval_is_bound_to_anchored_configuration_bytes(self) -> None:
        self.assertIn("install_files.ReadFile(kConfigName, kMaxUpdaterConfigBytes)", SOURCE)
        self.assertIn("options.approved_config_sha256 != config_sha256", SOURCE)
        self.assertIn('args.push_back("--approved-config-sha256")', SOURCE)
        self.assertIn("elevation_options.install_root = RequireProtectedElevationRoot(", SOURCE)
        self.assertIn("CurrentTokenHasPathAccess(config_path, access, false)", SOURCE)
        self.assertIn("FILE_DELETE_CHILD", SOURCE)
        self.assertIn(
            "if (!token_elevation.TokenIsElevated)", INSTALL_ROOT_SOURCE
        )
        self.assertIn("OWNER_SECURITY_INFORMATION", INSTALL_ROOT_SOURCE)
        self.assertIn(
            "ValidatePrivateFileAcl(temporary.Get(), destination_path)",
            INSTALL_ROOT_SOURCE,
        )
        self.assertIn("elevation_options.install_root / kUpdaterStem", SOURCE)
        self.assertIn("RelaunchElevated(elevation_worker, args", SOURCE)
        self.assertIn("LaunchProtectedElevatedWorker", SOURCE)
        self.assertIn("kElevatedWorkerPath", SOURCE)
        self.assertIn('args.push_back("--approved-release-index-sha256")', SOURCE)
        self.assertIn("ValidateApprovedReleaseBinding", SOURCE)

    def test_windows_worker_process_inherits_only_explicit_stdio_handles(self) -> None:
        launch_start = SOURCE.index("void LaunchWindowsProcess")
        launch_end = SOURCE.index("bool RelaunchElevated", launch_start)
        launch = SOURCE[launch_start:launch_end]
        self.assertIn("STARTUPINFOEXW", launch)
        self.assertIn("PROC_THREAD_ATTRIBUTE_HANDLE_LIST", launch)
        self.assertIn("EXTENDED_STARTUPINFO_PRESENT", launch)
        self.assertIn("std::array<HANDLE, 3> inherited_handles", launch)
        self.assertNotIn("STARTUPINFOW startup_info", launch)

    def test_windows_handoff_wait_binds_pid_to_process_creation_time(self) -> None:
        wait_start = SOURCE.index("void WaitForBootstrapProcessExit")
        wait_end = SOURCE.index("fs::path CurrentProcessImagePath", wait_start)
        wait = SOURCE[wait_start:wait_end]
        invocation_start = SOURCE.index("std::vector<std::string> BuildWorkerInvocation")
        invocation_end = SOURCE.index("int LaunchApprovedUpdateWorker", invocation_start)
        invocation = SOURCE[invocation_start:invocation_end]
        protected_start = SOURCE.index("int LaunchProtectedElevatedWorker")
        protected_end = SOURCE.index("int RelaunchInstalledBootstrap", protected_start)
        protected = SOURCE[protected_start:protected_end]
        flow_start = SOURCE.index("int RunBootstrapFlow")
        flow_end = SOURCE.index("BootstrapOptions ParseBootstrapOptions", flow_start)
        flow = SOURCE[flow_start:flow_end]
        parser_start = flow_end
        parser_end = SOURCE.index("} // namespace", parser_start)
        parser = SOURCE[parser_start:parser_end]
        identity_check = wait.index(
            "observed_creation_time != expected_creation_time"
        )
        blocking_wait = wait.index("WaitForSingleObject")

        self.assertIn("PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE", wait)
        self.assertIn("ProcessCreationTime(process)", wait)
        self.assertLess(identity_check, blocking_wait)
        self.assertIn("if (result == WAIT_TIMEOUT)", wait)
        self.assertIn("result == WAIT_FAILED ? GetLastError()", wait)
        self.assertIn("Could not wait for updater handoff process: ", wait)
        self.assertIn("Incomplete updater handoff process identity", invocation)
        self.assertIn('args.push_back("--wait-for-process")', invocation)
        self.assertIn(
            'args.push_back("--wait-for-process-creation-time")', invocation
        )
        self.assertIn(
            'if (arg == "--wait-for-process-creation-time" && i + 1 < argc)',
            parser,
        )
        self.assertIn("Incomplete updater handoff process identity", parser)
        self.assertIn(
            "WaitForBootstrapProcessExit(options.wait_for_process_id,\n"
            "                                options.wait_for_process_creation_time)",
            flow,
        )
        self.assertIn(
            "child_options.wait_for_process_id = GetCurrentProcessId();",
            protected,
        )
        self.assertIn(
            "child_options.wait_for_process_creation_time =\n"
            "      CurrentProcessCreationTime();",
            protected,
        )
        self.assertIn(
            "recovery_options.wait_for_process_id = GetCurrentProcessId();",
            flow,
        )
        self.assertIn(
            "recovery_options.wait_for_process_creation_time =\n"
            "          CurrentProcessCreationTime();",
            flow,
        )

    def test_stale_removal_requires_authenticated_prior_release_and_live_hash(self) -> None:
        plan_start = SOURCE.index("InstallSyncPlan BuildInstallSyncPlan")
        plan_end = SOURCE.index("std::string DescribeInstallSyncPlan", plan_start)
        plan = SOURCE[plan_start:plan_end]
        self.assertIn("local_manifest.authenticated_files", plan)
        self.assertIn("LiveFileMatchesEntry(installed_files, local_file)", plan)
        self.assertIn('root["installed_release_authentication"]', SOURCE)
        self.assertIn("RevalidateCachedRemotePayload(&authenticated_payload", SOURCE)
        self.assertIn("BuildLocalInstallManifest(authenticated_payload) != root", SOURCE)

    def test_same_payload_new_release_evidence_refreshes_the_local_baseline(self) -> None:
        plan_start = SOURCE.index("InstallSyncPlan BuildInstallSyncPlan")
        plan_end = SOURCE.index("std::string DescribeInstallSyncPlan", plan_start)
        plan = SOURCE[plan_start:plan_end]
        self.assertIn("!local_manifest.authenticated_files", plan)
        self.assertIn("local_manifest.authenticated_release_sequence !=", plan)
        self.assertIn(
            "local_manifest.authenticated_release_index_sha256 !=", plan
        )

    def test_cached_update_reverifies_exact_signed_bytes_and_current_policy(self) -> None:
        revalidation_start = SOURCE.index("void RevalidateCachedRemotePayload")
        revalidation_end = SOURCE.index(
            "LoadPendingUpdate(const AnchoredFileTree", revalidation_start
        )
        revalidation = SOURCE[revalidation_start:revalidation_end]
        self.assertIn("payload->raw_release_index", revalidation)
        self.assertIn("payload->raw_release_signature", revalidation)
        self.assertIn("VerifyReleaseIndexSignature(", revalidation)
        self.assertIn("payload->raw_update_manifest.size() != manifest_integrity.size", revalidation)
        self.assertIn("Sha256Hex(", revalidation)
        self.assertIn("void ValidateReleaseVerificationContext", SOURCE)
        self.assertIn("payload.release_index_name != config.release_index_asset", SOURCE)
        self.assertIn("authorized < config.release_trust_policy->threshold", SOURCE)
        self.assertIn("ValidateReleaseVerificationContext(payload, config);", SOURCE)

    def test_signed_freshness_revocation_and_anti_rollback_are_enforced(self) -> None:
        discovery_start = SOURCE.index("DiscoverRemotePayload(const UpdaterConfig")
        discovery_end = SOURCE.index("void ExtractZip", discovery_start)
        discovery = SOURCE[discovery_start:discovery_end]
        self.assertIn("ValidateReleaseIndexSecurityMetadata(index_json)", discovery)
        self.assertIn("(*observed_revocations)[revoked_sha256]", discovery)
        self.assertIn("revocation->second >= security.sequence", discovery)
        self.assertIn("Release index was revoked by a newer authenticated candidate", discovery)

        self.assertIn('JsonUInt64(release_index, "release_sequence", 0)', SOURCE)
        self.assertIn('JsonString(release_index, "expires_at_utc")', SOURCE)
        self.assertIn('release_index["revoked_release_index_sha256s"]', SOURCE)
        self.assertIn("ValidateReleaseSequenceAgainstInstalled(discovered, local_manifest)", SOURCE)
        self.assertIn("ValidateReleaseSequenceAgainstInstalled(payload, installed)", discovery)
        self.assertIn("revocation->second >= pending->release_sequence", SOURCE)
        self.assertIn("Cached update was revoked by an authenticated release index", SOURCE)

    def test_installed_authentication_keeps_sequence_after_release_expiry(self) -> None:
        self.assertIn(
            "RevalidateCachedRemotePayload(&authenticated_payload, config, options,\n"
            "                                  false);",
            SOURCE,
        )
        self.assertIn("if (require_current_freshness)", SOURCE)

    def test_https_and_authenticated_package_size_are_enforced(self) -> None:
        self.assertIn('curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https")', SOURCE)
        self.assertIn("CURLOPT_MAXFILESIZE_LARGE", SOURCE)
        self.assertIn("output.bytes_written != expected_size", SOURCE)
        self.assertIn("expected_size > kMaxUpdatePackageBytes", SOURCE)
        self.assertIn("available_update->update_package_integrity.size", SOURCE)
        self.assertIn("protocols != CURLE_OK || redirect_protocols != CURLE_OK", SOURCE)

    def test_signed_manifest_identity_and_native_bounds_are_enforced(self) -> None:
        self.assertIn('JsonUInt64(manifest, "schema_version", 0) != 3', SOURCE)
        self.assertIn("files.size() > kMaxManagedFiles", SOURCE)
        self.assertIn("ValidateSignedManifestTargetIdentity(manifest_json, index_json", SOURCE)
        self.assertIn('JsonString(build, "commit_sha") != signed_commit', SOURCE)
        self.assertIn("payload.local_manifest_name != kLocalManifestName", SOURCE)

    def test_live_file_match_uses_anchored_same_handle_hashing(self) -> None:
        matcher_start = SOURCE.index("bool LiveFileMatchesEntry")
        matcher_end = SOURCE.index("InstallSyncPlan BuildInstallSyncPlan", matcher_start)
        matcher = SOURCE[matcher_start:matcher_end]
        self.assertIn("installed_files.FileMatches(", matcher)
        self.assertIn(
            "MakeInstallFileExpectation(entry.size, entry.sha256, entry.mode)",
            matcher,
        )

    def test_failed_update_staging_is_cleaned_on_every_exit(self) -> None:
        self.assertIn("struct UpdateStagingCleanup", SOURCE)
        self.assertIn("} staging_cleanup{temp_root};", SOURCE)


if __name__ == "__main__":
    unittest.main()
