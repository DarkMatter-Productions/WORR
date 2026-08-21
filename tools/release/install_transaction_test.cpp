#include "src/updater/install_transaction.h"
#include "src/updater/install_root.h"
#include "src/updater/sha256.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using worr::updater::ApplyInstallTransaction;
using worr::updater::AnchoredFileTree;
using worr::updater::InstallTransactionCheckpoint;
using worr::updater::InstallTransactionOptions;
using worr::updater::InstallTransactionPhase;
using worr::updater::InstallTransactionPlan;
using worr::updater::InstallTransactionRecoveryResult;
using worr::updater::RecoverInstallTransaction;
using worr::updater::Sha256Hex;
using worr::updater::SimulatedInstallTransactionCrash;

namespace {

namespace fs = std::filesystem;

int failures = 0;

void Fail(std::string_view message) {
  std::fprintf(stderr, "%.*s\n", static_cast<int>(message.size()),
               message.data());
  ++failures;
}

void WriteFile(const fs::path &path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output)
    throw std::runtime_error("Could not write fixture: " + path.string());
}

std::string ReadFile(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input)
    throw std::runtime_error("Could not read fixture: " + path.string());
  return contents.str();
}

#if !defined(_WIN32)
void SetMode(const fs::path &path, fs::perms mode) {
  fs::permissions(path, mode, fs::perm_options::replace);
}

fs::perms PortableMode(const fs::path &path) {
  constexpr fs::perms mask =
      fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
      fs::perms::group_read | fs::perms::group_write | fs::perms::group_exec |
      fs::perms::others_read | fs::perms::others_write | fs::perms::others_exec;
  return fs::status(path).permissions() & mask;
}
#endif

struct FileSnapshot {
  bool exists = false;
  std::string contents;
#if !defined(_WIN32)
  fs::perms mode = fs::perms::unknown;
#endif
};

using InstallSnapshot = std::map<std::string, FileSnapshot>;

const std::vector<std::string> &ManagedPaths() {
  static const std::vector<std::string> paths = {
      "bin/add.dat",
      "bin/replace.dat",
      "obsolete/remove.dat",
      "state/install.json",
  };
  return paths;
}

InstallSnapshot Snapshot(const fs::path &root) {
  InstallSnapshot snapshot;
  for (const std::string &relative_path : ManagedPaths()) {
    const fs::path path = root / fs::u8path(relative_path);
    FileSnapshot file;
    file.exists = fs::is_regular_file(path);
    if (file.exists) {
      file.contents = ReadFile(path);
#if !defined(_WIN32)
      file.mode = PortableMode(path);
#endif
    }
    snapshot.emplace(relative_path, std::move(file));
  }
  return snapshot;
}

bool SnapshotsEqual(const InstallSnapshot &left, const InstallSnapshot &right) {
  if (left.size() != right.size())
    return false;
  for (const auto &[path, expected] : left) {
    const auto actual = right.find(path);
    if (actual == right.end() || expected.exists != actual->second.exists ||
        expected.contents != actual->second.contents)
      return false;
#if !defined(_WIN32)
    if (expected.exists && expected.mode != actual->second.mode)
      return false;
#endif
  }
  return true;
}

bool HasOperationTemporaryFile(const fs::path &root) {
  for (const fs::directory_entry &entry :
       fs::recursive_directory_iterator(root)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind(".worr-update-", 0) == 0 && name.size() >= 4 &&
        name.substr(name.size() - 4) == ".tmp")
      return true;
  }
  return false;
}

struct Fixture {
  fs::path root;
  fs::path stage;
  fs::path install;
  fs::path rollback;
  InstallTransactionPlan plan;

  explicit Fixture(std::string_view label) {
    static std::size_t sequence = 0;
    root = fs::temp_directory_path() /
           ("worr-install-transaction-test-" + std::string(label) + "-" +
            std::to_string(std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count()) +
            "-" + std::to_string(sequence++));
    stage = root / "stage";
    install = root / "install";
    rollback = root / "rollback";
    fs::create_directories(stage);
    fs::create_directories(install);
    fs::create_directories(rollback);

    WriteFile(stage / "bin" / "add.dat", "added-v2");
    WriteFile(stage / "bin" / "replace.dat", "replacement-v2");
    WriteFile(install / "bin" / "replace.dat", "replacement-v1");
    WriteFile(install / "obsolete" / "remove.dat", "remove-v1");
    WriteFile(install / "state" / "install.json",
              "{\"version\":1,\"format\":\"exact\"}\n");
    WriteFile(install / "worr_install.lock", "");
#if !defined(_WIN32)
    SetMode(stage / "bin" / "add.dat",
            fs::perms::owner_read | fs::perms::owner_write |
                fs::perms::owner_exec | fs::perms::group_read |
                fs::perms::group_exec | fs::perms::others_read |
                fs::perms::others_exec);
    SetMode(stage / "bin" / "replace.dat", fs::perms::owner_read |
                                               fs::perms::owner_write |
                                               fs::perms::owner_exec);
    SetMode(install / "bin" / "replace.dat", fs::perms::owner_read |
                                                 fs::perms::owner_write |
                                                 fs::perms::group_read);
    SetMode(install / "obsolete" / "remove.dat",
            fs::perms::owner_read | fs::perms::owner_write |
                fs::perms::owner_exec | fs::perms::group_exec);
    SetMode(install / "state" / "install.json",
            fs::perms::owner_read | fs::perms::owner_write |
                fs::perms::group_read | fs::perms::others_read);
    SetMode(install / "worr_install.lock",
            fs::perms::owner_read | fs::perms::owner_write |
                fs::perms::group_read | fs::perms::others_read);
#endif

    plan.install_files = {
        {"bin/add.dat", 8,
         Sha256Hex(reinterpret_cast<const uint8_t *>("added-v2"), 8)},
        {"bin/replace.dat", 14,
         Sha256Hex(reinterpret_cast<const uint8_t *>("replacement-v2"), 14)},
    };
    plan.remove_paths = {"obsolete/remove.dat"};
    plan.manifest_path = "state/install.json";
    plan.manifest_contents = "{\"version\":2,\"format\":\"replacement\"}\n";
  }

  ~Fixture() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

struct FaultPoint {
  InstallTransactionPhase phase;
  InstallTransactionCheckpoint checkpoint;
  std::string path;
};

worr::updater::InstallTransactionFile
PlanFile(std::string path, std::string_view contents, uint32_t mode = 0) {
  return {std::move(path), static_cast<uint64_t>(contents.size()),
          Sha256Hex(reinterpret_cast<const uint8_t *>(contents.data()),
                    contents.size()),
          mode};
}

void ConfigureFileToDirectory(Fixture &fixture) {
  constexpr std::string_view contents = "nested-v2";
  WriteFile(fixture.install / "shape", "flat-v1");
  WriteFile(fixture.stage / "shape" / "deep" / "new.dat", contents);
#if !defined(_WIN32)
  SetMode(fixture.install / "shape", fs::perms::owner_read |
                                          fs::perms::owner_write |
                                          fs::perms::group_exec);
#endif
  fixture.plan.install_files = {
      PlanFile("shape/deep/new.dat", contents, 0644)};
  fixture.plan.remove_paths = {"shape"};
}

void ConfigureDirectoryToFile(Fixture &fixture) {
  constexpr std::string_view contents = "flat-v2";
  WriteFile(fixture.install / "shape" / "deep" / "old.dat", "nested-v1");
  WriteFile(fixture.stage / "shape", contents);
#if !defined(_WIN32)
  SetMode(fixture.install / "shape",
          fs::perms::owner_all | fs::perms::group_exec |
              fs::perms::others_exec);
  SetMode(fixture.install / "shape" / "deep",
          fs::perms::owner_all | fs::perms::group_read |
              fs::perms::group_exec);
  SetMode(fixture.install / "shape" / "deep" / "old.dat",
          fs::perms::owner_read | fs::perms::owner_write |
              fs::perms::owner_exec | fs::perms::group_read);
#endif
  fixture.plan.install_files = {PlanFile("shape", contents, 0644)};
  fixture.plan.remove_paths = {"shape/deep/old.dat"};
}

void ExpectCompleteRollback(std::string_view label, const FaultPoint &fault,
                            bool permission_error = false) {
  Fixture fixture(label);
  const InstallSnapshot before = Snapshot(fixture.install);
  bool injected = false;
  InstallTransactionOptions options;
  options.hook = [&](InstallTransactionPhase phase,
                     InstallTransactionCheckpoint checkpoint,
                     std::string_view path) {
    if (!injected && phase == fault.phase && checkpoint == fault.checkpoint &&
        path == fault.path) {
      injected = true;
      if (permission_error) {
        throw std::system_error(
            std::make_error_code(std::errc::permission_denied),
            "simulated locked file");
      }
      throw std::runtime_error("injected apply failure");
    }
  };

  bool rejected = false;
  std::string rejection_message;
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, options);
  } catch (const std::exception &error) {
    rejected = true;
    rejection_message = error.what();
  }
  if (!rejected || !injected) {
    std::fprintf(stderr, "%.*s detail: injected=%d rejected=%d error=%s\n",
                 static_cast<int>(label.size()), label.data(), injected ? 1 : 0,
                 rejected ? 1 : 0, rejection_message.c_str());
    Fail(std::string(label) + ": expected fault was not propagated");
  }
  if (!SnapshotsEqual(before, Snapshot(fixture.install)))
    Fail(std::string(label) +
         ": rollback did not restore exact bytes and permissions");
  if (HasOperationTemporaryFile(fixture.install) ||
      HasOperationTemporaryFile(fixture.rollback))
    Fail(std::string(label) + ": operation temporary file was not cleaned");
}

void TestSuccess() {
  Fixture fixture("success");
  std::vector<std::pair<std::size_t, std::size_t>> progress;
  InstallTransactionOptions options;
  options.progress = [&](std::size_t completed, std::size_t total) {
    progress.emplace_back(completed, total);
  };
  ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                          fixture.plan, options);

  if (ReadFile(fixture.install / "bin" / "add.dat") != "added-v2" ||
      ReadFile(fixture.install / "bin" / "replace.dat") != "replacement-v2" ||
      fs::exists(fixture.install / "obsolete" / "remove.dat") ||
      ReadFile(fixture.install / "state" / "install.json") !=
          fixture.plan.manifest_contents)
    Fail("successful transaction produced the wrong install tree");
#if !defined(_WIN32)
  if (PortableMode(fixture.install / "bin" / "add.dat") !=
          PortableMode(fixture.stage / "bin" / "add.dat") ||
      PortableMode(fixture.install / "bin" / "replace.dat") !=
          PortableMode(fixture.stage / "bin" / "replace.dat"))
    Fail("successful transaction did not preserve staged executable modes");
#endif
  const std::vector<std::pair<std::size_t, std::size_t>> expected_progress = {
      {1, 3}, {2, 3}, {3, 3}};
  if (progress != expected_progress)
    Fail("successful transaction reported incorrect progress");
  if (HasOperationTemporaryFile(fixture.install) ||
      HasOperationTemporaryFile(fixture.rollback))
    Fail("successful transaction left an operation temporary file");
}

void TestApplyFaults() {
  std::vector<std::pair<std::string, FaultPoint>> faults = {
      {"backup-temp-created",
       {InstallTransactionPhase::Backup,
        InstallTransactionCheckpoint::AfterTemporaryFileCreated,
        "bin/replace.dat"}},
      {"backup-temp-written",
       {InstallTransactionPhase::Backup,
        InstallTransactionCheckpoint::AfterTemporaryFileWritten,
        "bin/replace.dat"}},
      {"backup-before-replace",
       {InstallTransactionPhase::Backup,
        InstallTransactionCheckpoint::BeforeAtomicReplacement,
        "bin/replace.dat"}},
      {"after-backup",
       {InstallTransactionPhase::Backup,
        InstallTransactionCheckpoint::AfterBackup, "bin/add.dat"}},
      {"install-temp-created",
       {InstallTransactionPhase::Install,
        InstallTransactionCheckpoint::AfterTemporaryFileCreated,
        "bin/add.dat"}},
      {"install-temp-written",
       {InstallTransactionPhase::Install,
        InstallTransactionCheckpoint::AfterTemporaryFileWritten,
        "bin/add.dat"}},
      {"install-before-replace",
       {InstallTransactionPhase::Install,
        InstallTransactionCheckpoint::BeforeAtomicReplacement, "bin/add.dat"}},
      {"after-install",
       {InstallTransactionPhase::Install,
        InstallTransactionCheckpoint::AfterInstall, "bin/replace.dat"}},
      {"remove-before-remove",
       {InstallTransactionPhase::Remove,
        InstallTransactionCheckpoint::BeforeRemoval, "obsolete/remove.dat"}},
      {"after-remove",
       {InstallTransactionPhase::Remove,
        InstallTransactionCheckpoint::AfterRemove, "obsolete/remove.dat"}},
      {"manifest-temp-created",
       {InstallTransactionPhase::Manifest,
        InstallTransactionCheckpoint::AfterTemporaryFileCreated,
        "state/install.json"}},
      {"manifest-temp-written",
       {InstallTransactionPhase::Manifest,
        InstallTransactionCheckpoint::AfterTemporaryFileWritten,
        "state/install.json"}},
      {"manifest-before-replace",
       {InstallTransactionPhase::Manifest,
        InstallTransactionCheckpoint::BeforeAtomicReplacement,
        "state/install.json"}},
      {"after-manifest",
       {InstallTransactionPhase::Manifest,
        InstallTransactionCheckpoint::AfterManifest, "state/install.json"}},
  };
#if !defined(_WIN32)
  faults.push_back({"remove-after-directory-fsync",
                    {InstallTransactionPhase::Remove,
                     InstallTransactionCheckpoint::AfterParentDirectoryFlushed,
                     "obsolete/remove.dat"}});
#endif
  for (const auto &[label, fault] : faults)
    ExpectCompleteRollback(label, fault, label == "remove-before-remove");
}

void TestProgressInterruption() {
  Fixture fixture("progress-interruption");
  const InstallSnapshot before = Snapshot(fixture.install);
  InstallTransactionOptions options;
  options.progress = [](std::size_t completed, std::size_t) {
    if (completed == 1)
      throw std::runtime_error("simulated interruption");
  };
  bool rejected = false;
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, options);
  } catch (const std::exception &error) {
    rejected = std::string(error.what()).find("simulated interruption") !=
               std::string::npos;
  }
  if (!rejected || !SnapshotsEqual(before, Snapshot(fixture.install)))
    Fail("progress interruption was not propagated after complete rollback");
}

void TestCopiedBytesAuthenticated() {
  for (std::string_view replacement : {"evil--v2", "short"}) {
    Fixture fixture(replacement == "short" ? "copy-size" : "copy-hash");
    const InstallSnapshot before = Snapshot(fixture.install);
    bool source_changed = false;
    InstallTransactionOptions options;
    options.hook = [&](InstallTransactionPhase phase,
                       InstallTransactionCheckpoint checkpoint,
                       std::string_view path) {
      if (!source_changed && phase == InstallTransactionPhase::Install &&
          checkpoint ==
              InstallTransactionCheckpoint::AfterTemporaryFileCreated &&
          path == "bin/add.dat") {
        WriteFile(fixture.stage / "bin" / "add.dat", replacement);
        source_changed = true;
      }
    };

    std::string error_message;
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                              fixture.plan, options);
    } catch (const std::exception &error) {
      error_message = error.what();
    }
    const std::string expected_error =
        replacement == "short" ? "size changed" : "hash changed";
    if (!source_changed ||
        error_message.find(expected_error) == std::string::npos)
      Fail("transaction did not authenticate the bytes copied from its open "
           "staging handle");
    if (!SnapshotsEqual(before, Snapshot(fixture.install)))
      Fail("copied-byte authentication failure did not restore the install");
    if (HasOperationTemporaryFile(fixture.install) ||
        HasOperationTemporaryFile(fixture.rollback))
      Fail("copied-byte authentication failure left a temporary file");
  }
}

void TestRollbackOrder() {
  Fixture fixture("rollback-order");
  const InstallSnapshot before = Snapshot(fixture.install);
  std::vector<std::string> rollback_paths;
  InstallTransactionOptions options;
  options.hook = [&](InstallTransactionPhase phase,
                     InstallTransactionCheckpoint checkpoint,
                     std::string_view path) {
    if (phase == InstallTransactionPhase::Manifest &&
        checkpoint == InstallTransactionCheckpoint::AfterManifest)
      throw std::runtime_error("fail after manifest");
    if (phase == InstallTransactionPhase::Rollback &&
        checkpoint == InstallTransactionCheckpoint::DuringRollback)
      rollback_paths.emplace_back(path);
  };
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, options);
    Fail("rollback ordering fixture unexpectedly succeeded");
  } catch (const std::exception &) {
  }

  const std::vector<std::string> expected = {
      "state/install.json", "bin/replace.dat", "bin/add.dat",
      "obsolete/remove.dat"};
  if (rollback_paths != expected)
    Fail("transaction did not roll back in exact reverse mutation order");
  if (!SnapshotsEqual(before, Snapshot(fixture.install)))
    Fail("reverse-order rollback did not restore the original install");
}

void TestIncompleteRollbackError() {
  Fixture fixture("incomplete-rollback");
  const InstallSnapshot before = Snapshot(fixture.install);
  bool rollback_fault_injected = false;
  InstallTransactionOptions options;
  options.hook = [&](InstallTransactionPhase phase,
                     InstallTransactionCheckpoint checkpoint,
                     std::string_view path) {
    if (phase == InstallTransactionPhase::Manifest &&
        checkpoint == InstallTransactionCheckpoint::AfterManifest)
      throw std::runtime_error("primary apply failure");
    if (!rollback_fault_injected &&
        phase == InstallTransactionPhase::Rollback &&
        checkpoint == InstallTransactionCheckpoint::DuringRollback &&
        path == "bin/replace.dat") {
      rollback_fault_injected = true;
      throw std::runtime_error("secondary rollback failure");
    }
  };

  std::string error_message;
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, options);
  } catch (const std::exception &error) {
    error_message = error.what();
  }
  if (!rollback_fault_injected ||
      error_message.find("rollback was incomplete") == std::string::npos ||
      error_message.find("primary apply failure") == std::string::npos ||
      error_message.find("secondary rollback failure") == std::string::npos) {
    std::fprintf(stderr, "incomplete rollback detail: injected=%d error=%s\n",
                 rollback_fault_injected ? 1 : 0, error_message.c_str());
    Fail("incomplete rollback did not produce an explicit combined error");
  }

  const InstallSnapshot after = Snapshot(fixture.install);
  for (const char *restored_path :
       {"bin/add.dat", "obsolete/remove.dat", "state/install.json"}) {
    if (after.at(restored_path).exists != before.at(restored_path).exists ||
        after.at(restored_path).contents != before.at(restored_path).contents)
      Fail("rollback stopped instead of restoring paths after one failure");
  }
  if (after.at("bin/replace.dat").contents != "replacement-v2")
    Fail("incomplete rollback fixture did not preserve the failed path state");
  if (HasOperationTemporaryFile(fixture.install) ||
      HasOperationTemporaryFile(fixture.rollback))
    Fail("incomplete rollback left an operation temporary file");
}

void TestManifestCreationRollback() {
  Fixture fixture("manifest-create-rollback");
  fs::remove(fixture.install / "state" / "install.json");
  InstallTransactionOptions options;
  options.hook = [](InstallTransactionPhase phase,
                    InstallTransactionCheckpoint checkpoint, std::string_view) {
    if (phase == InstallTransactionPhase::Manifest &&
        checkpoint == InstallTransactionCheckpoint::AfterManifest)
      throw std::runtime_error("fail after new manifest");
  };
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, options);
    Fail("new-manifest rollback fixture unexpectedly succeeded");
  } catch (const std::exception &) {
  }
  if (fs::exists(fixture.install / "state" / "install.json"))
    Fail("rollback did not remove a newly created install manifest");
}

void TestDuplicatePathRejectedBeforeMutation() {
  Fixture fixture("duplicate-path");
  const InstallSnapshot before = Snapshot(fixture.install);
  fixture.plan.remove_paths.push_back("BIN/ADD.DAT");
  bool rejected = false;
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan);
  } catch (const std::exception &) {
    rejected = true;
  }
  if (!rejected || !SnapshotsEqual(before, Snapshot(fixture.install)))
    Fail("colliding transaction paths were not rejected before mutation");
}

void TestInterruptedApplyRestartRecovery() {
  Fixture fixture("restart-recovery");
  const InstallSnapshot before = Snapshot(fixture.install);
  InstallTransactionOptions crash_options;
  crash_options.hook = [](InstallTransactionPhase phase,
                          InstallTransactionCheckpoint checkpoint,
                          std::string_view path) {
    if (phase == InstallTransactionPhase::Install &&
        checkpoint == InstallTransactionCheckpoint::AfterInstall &&
        path == "bin/replace.dat")
      throw SimulatedInstallTransactionCrash("simulated process termination");
  };

  bool crashed = false;
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, crash_options);
  } catch (const SimulatedInstallTransactionCrash &) {
    crashed = true;
  }
  if (!crashed ||
      !fs::is_regular_file(fixture.install / ".worr-update" / "journal"))
    Fail("simulated process termination did not retain a durable journal");
  if (SnapshotsEqual(before, Snapshot(fixture.install)))
    Fail("restart recovery fixture did not stop after a partial apply");

  const InstallTransactionRecoveryResult recovery =
      RecoverInstallTransaction(fixture.install);
  if (recovery !=
          InstallTransactionRecoveryResult::RolledBackInterruptedApply ||
      !SnapshotsEqual(before, Snapshot(fixture.install)) ||
      fs::exists(fixture.install / ".worr-update" / "journal"))
    Fail("restart recovery did not restore and clean an interrupted apply");
  if (RecoverInstallTransaction(fixture.install) !=
      InstallTransactionRecoveryResult::NothingToRecover)
    Fail("idempotent restart recovery reported stale transaction state");
}

void TestRecoveryCanResumeAfterSecondInterruption() {
  Fixture fixture("recovery-resume");
  const InstallSnapshot before = Snapshot(fixture.install);
  InstallTransactionOptions crash_options;
  crash_options.hook = [](InstallTransactionPhase phase,
                          InstallTransactionCheckpoint checkpoint,
                          std::string_view path) {
    if (phase == InstallTransactionPhase::Remove &&
        checkpoint == InstallTransactionCheckpoint::AfterRemove &&
        path == "obsolete/remove.dat")
      throw SimulatedInstallTransactionCrash("stop during apply");
  };
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, crash_options);
  } catch (const SimulatedInstallTransactionCrash &) {
  }

  bool recovery_interrupted = false;
  InstallTransactionOptions recovery_options;
  recovery_options.hook = [&](InstallTransactionPhase phase,
                              InstallTransactionCheckpoint checkpoint,
                              std::string_view) {
    if (!recovery_interrupted && phase == InstallTransactionPhase::Rollback &&
        checkpoint == InstallTransactionCheckpoint::DuringRollback) {
      recovery_interrupted = true;
      throw SimulatedInstallTransactionCrash("stop during recovery");
    }
  };
  try {
    (void)RecoverInstallTransaction(fixture.install, recovery_options);
    Fail("restart recovery interruption was not propagated");
  } catch (const SimulatedInstallTransactionCrash &) {
  }
  if (!recovery_interrupted ||
      !fs::exists(fixture.install / ".worr-update" / "journal"))
    Fail("interrupted recovery did not retain its durable journal");

  if (RecoverInstallTransaction(fixture.install) !=
          InstallTransactionRecoveryResult::RolledBackInterruptedApply ||
      !SnapshotsEqual(before, Snapshot(fixture.install)))
    Fail("a second restart could not complete deterministic recovery");
}

void TestLegacyV2JournalRecovery() {
  Fixture fixture("legacy-v2-recovery");
  const InstallSnapshot before = Snapshot(fixture.install);
  InstallTransactionOptions options;
  options.hook = [](InstallTransactionPhase phase,
                    InstallTransactionCheckpoint checkpoint,
                    std::string_view path) {
    if (phase == InstallTransactionPhase::Install &&
        checkpoint == InstallTransactionCheckpoint::AfterInstall &&
        path == "bin/replace.dat")
      throw SimulatedInstallTransactionCrash("retain version-three journal");
  };
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan,
                            options);
  } catch (const SimulatedInstallTransactionCrash &) {
  }

  const fs::path journal_path = fixture.install / ".worr-update" / "journal";
  std::string journal = ReadFile(journal_path);
  const std::string version_three = "WORR_INSTALL_TRANSACTION_V3";
  if (journal.rfind(version_three, 0) != 0) {
    Fail("legacy recovery fixture did not produce a version-three journal");
    return;
  }
  journal.replace(0, version_three.size(), "WORR_INSTALL_TRANSACTION_V2");
  for (std::size_t offset = 0;
       (offset = journal.find("\nentry ", offset)) != std::string::npos;) {
    const std::size_t kind = offset + 7;
    if (journal[kind] == 'f')
      journal[kind] = '1';
    else if (journal[kind] == 'a')
      journal[kind] = '0';
    else
      Fail("legacy recovery fixture unexpectedly contained a directory entry");
    offset = kind + 1;
  }
  WriteFile(journal_path, journal);
  if (RecoverInstallTransaction(fixture.install) !=
          InstallTransactionRecoveryResult::RolledBackInterruptedApply ||
      !SnapshotsEqual(before, Snapshot(fixture.install)))
    Fail("version-two journal compatibility recovery failed");
}

void TestCommittedMarkerNeverRollsBack() {
  Fixture fixture("committed-recovery");
  InstallTransactionOptions options;
  options.hook = [](InstallTransactionPhase phase,
                    InstallTransactionCheckpoint checkpoint, std::string_view) {
    if (phase == InstallTransactionPhase::Commit &&
        checkpoint == InstallTransactionCheckpoint::AfterCommitMarker)
      throw SimulatedInstallTransactionCrash("stop after durable commit");
  };
  bool crashed = false;
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, options);
  } catch (const SimulatedInstallTransactionCrash &) {
    crashed = true;
  }
  if (!crashed)
    Fail("post-commit interruption was not propagated");

  const InstallSnapshot committed = Snapshot(fixture.install);
  if (ReadFile(fixture.install / "bin" / "replace.dat") != "replacement-v2" ||
      fs::exists(fixture.install / "obsolete" / "remove.dat"))
    Fail("post-commit interruption fixture was not fully applied");
  if (RecoverInstallTransaction(fixture.install) !=
          InstallTransactionRecoveryResult::CleanedCommittedTransaction ||
      !SnapshotsEqual(committed, Snapshot(fixture.install)))
    Fail("recovery rolled back content after the durable commit marker");
}

void TestPrivatePersistentStatePermissions() {
#if !defined(_WIN32)
  Fixture fixture("private-state");
  InstallTransactionOptions options;
  options.hook = [](InstallTransactionPhase phase,
                    InstallTransactionCheckpoint checkpoint,
                    std::string_view path) {
    if (phase == InstallTransactionPhase::Install &&
        checkpoint == InstallTransactionCheckpoint::AfterInstall &&
        path == "bin/add.dat")
      throw SimulatedInstallTransactionCrash("inspect private state");
  };
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan, options);
  } catch (const SimulatedInstallTransactionCrash &) {
  }
  constexpr fs::perms mask =
      fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all;
  const fs::path state = fixture.install / ".worr-update";
  if ((fs::status(state).permissions() & mask) !=
          (fs::perms::owner_all | fs::perms::group_read |
           fs::perms::group_exec | fs::perms::others_read |
           fs::perms::others_exec) ||
      (fs::status(state / "rollback").permissions() & mask) !=
          fs::perms::owner_all ||
      (fs::status(state / "journal").permissions() & mask) !=
          (fs::perms::owner_read | fs::perms::owner_write))
    Fail("persistent transaction state did not use 0755/0700/0600 permissions");
  (void)RecoverInstallTransaction(fixture.install);
#endif
}

void TestShapeTransitionSuccess() {
  {
    Fixture fixture("file-to-directory-success");
    ConfigureFileToDirectory(fixture);
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan);
    if (!fs::is_directory(fixture.install / "shape") ||
        !fs::is_directory(fixture.install / "shape" / "deep") ||
        ReadFile(fixture.install / "shape" / "deep" / "new.dat") !=
            "nested-v2")
      Fail("file-to-directory transaction did not publish the target tree");
  }
  {
    Fixture fixture("directory-to-file-success");
    ConfigureDirectoryToFile(fixture);
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan);
    if (!fs::is_regular_file(fixture.install / "shape") ||
        ReadFile(fixture.install / "shape") != "flat-v2")
      Fail("directory-to-file transaction did not publish the target file");
  }
}

void TestShapeTransitionRollback() {
  {
    Fixture fixture("file-to-directory-rollback");
    ConfigureFileToDirectory(fixture);
#if !defined(_WIN32)
    const fs::perms original_mode = PortableMode(fixture.install / "shape");
#endif
    InstallTransactionOptions options;
    options.hook = [](InstallTransactionPhase phase,
                      InstallTransactionCheckpoint checkpoint,
                      std::string_view path) {
      if (phase == InstallTransactionPhase::Install &&
          checkpoint == InstallTransactionCheckpoint::AfterInstall &&
          path == "shape/deep/new.dat")
        throw std::runtime_error("fail after deep file install");
    };
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan,
                              options);
      Fail("file-to-directory rollback fixture unexpectedly succeeded");
    } catch (const std::exception &) {
    }
    if (!fs::is_regular_file(fixture.install / "shape") ||
        ReadFile(fixture.install / "shape") != "flat-v1")
      Fail("file-to-directory rollback did not restore the original file");
#if !defined(_WIN32)
    if (PortableMode(fixture.install / "shape") != original_mode)
      Fail("file-to-directory rollback did not restore the original mode");
#endif
  }

  {
    Fixture fixture("directory-to-file-rollback");
    ConfigureDirectoryToFile(fixture);
#if !defined(_WIN32)
    const fs::perms root_mode = PortableMode(fixture.install / "shape");
    const fs::perms deep_mode =
        PortableMode(fixture.install / "shape" / "deep");
    const fs::perms file_mode =
        PortableMode(fixture.install / "shape" / "deep" / "old.dat");
#endif
    InstallTransactionOptions options;
    options.hook = [](InstallTransactionPhase phase,
                      InstallTransactionCheckpoint checkpoint,
                      std::string_view path) {
      if (phase == InstallTransactionPhase::Install &&
          checkpoint == InstallTransactionCheckpoint::AfterInstall &&
          path == "shape")
        throw std::runtime_error("fail after flat file install");
    };
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan,
                              options);
      Fail("directory-to-file rollback fixture unexpectedly succeeded");
    } catch (const std::exception &) {
    }
    if (!fs::is_directory(fixture.install / "shape" / "deep") ||
        ReadFile(fixture.install / "shape" / "deep" / "old.dat") !=
            "nested-v1")
      Fail("directory-to-file rollback did not restore the original tree");
#if !defined(_WIN32)
    if (PortableMode(fixture.install / "shape") != root_mode ||
        PortableMode(fixture.install / "shape" / "deep") != deep_mode ||
        PortableMode(fixture.install / "shape" / "deep" / "old.dat") !=
            file_mode)
      Fail("directory-to-file rollback did not restore exact POSIX modes");
#endif
  }
}

void TestShapeTransitionRestartRecoveryAndRuntimeGuard() {
  {
    Fixture fixture("file-to-directory-recovery");
    ConfigureFileToDirectory(fixture);
    InstallTransactionOptions options;
    options.hook = [](InstallTransactionPhase phase,
                      InstallTransactionCheckpoint checkpoint,
                      std::string_view path) {
      if (phase == InstallTransactionPhase::Install &&
          checkpoint == InstallTransactionCheckpoint::AfterInstall &&
          path == "shape/deep")
        throw SimulatedInstallTransactionCrash(
            "stop after deep directory creation");
    };
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan,
                              options);
      Fail("file-to-directory recovery fixture unexpectedly succeeded");
    } catch (const SimulatedInstallTransactionCrash &) {
    }
    {
      AnchoredFileTree installed(fixture.install);
      auto runtime = installed.AcquireExistingFileSharedLock(
          "worr_install.lock");
      bool rejected = false;
      try {
        (void)RecoverInstallTransaction(fixture.install);
      } catch (const std::exception &) {
        rejected = true;
      }
      if (!rejected ||
          !fs::exists(fixture.install / ".worr-update" / "journal"))
        Fail("restart recovery ignored the active runtime shared guard");
    }
    if (RecoverInstallTransaction(fixture.install) !=
            InstallTransactionRecoveryResult::RolledBackInterruptedApply ||
        !fs::is_regular_file(fixture.install / "shape") ||
        ReadFile(fixture.install / "shape") != "flat-v1")
      Fail("restart recovery did not restore a deep file-to-directory change");
  }

  {
    Fixture fixture("directory-to-file-recovery");
    ConfigureDirectoryToFile(fixture);
    InstallTransactionOptions options;
    options.hook = [](InstallTransactionPhase phase,
                      InstallTransactionCheckpoint checkpoint,
                      std::string_view path) {
      if (phase == InstallTransactionPhase::Remove &&
          checkpoint == InstallTransactionCheckpoint::AfterRemove &&
          path == "shape")
        throw SimulatedInstallTransactionCrash(
            "stop after directory transition removal");
    };
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan,
                              options);
      Fail("directory-to-file recovery fixture unexpectedly succeeded");
    } catch (const SimulatedInstallTransactionCrash &) {
    }
    if (RecoverInstallTransaction(fixture.install) !=
            InstallTransactionRecoveryResult::RolledBackInterruptedApply ||
        !fs::is_directory(fixture.install / "shape" / "deep") ||
        ReadFile(fixture.install / "shape" / "deep" / "old.dat") !=
            "nested-v1")
      Fail("restart recovery did not restore a deep directory-to-file change");
  }
}

void TestShapeTransitionPreflightRejectsUnownedContents() {
  Fixture fixture("directory-to-file-unowned");
  ConfigureDirectoryToFile(fixture);
  WriteFile(fixture.install / "shape" / "user.dat", "do-not-remove");
  bool rejected = false;
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan);
  } catch (const std::exception &error) {
    rejected = std::string(error.what()).find("unowned file") !=
               std::string::npos;
  }
  if (!rejected ||
      ReadFile(fixture.install / "shape" / "user.dat") != "do-not-remove" ||
      ReadFile(fixture.install / "shape" / "deep" / "old.dat") !=
          "nested-v1" ||
      fs::exists(fixture.install / ".worr-update"))
    Fail("shape-transition preflight mutated or accepted unowned contents");
}

void TestRuntimeGuardRejectsConcurrentApply() {
  Fixture fixture("runtime-guard");
  {
    AnchoredFileTree installed(fixture.install);
    auto runtime =
        installed.AcquireExistingFileSharedLock("worr_install.lock");
    bool rejected = false;
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan);
    } catch (const std::exception &) {
      rejected = true;
    }
    if (!rejected || fs::exists(fixture.install / ".worr-update"))
      Fail("transaction ignored an active runtime shared guard");
  }
}

void TestRuntimeGuardMetadataValidation() {
  {
    Fixture fixture("runtime-guard-content");
    const InstallSnapshot before = Snapshot(fixture.install);
    WriteFile(fixture.install / "worr_install.lock", "not-empty");
    bool rejected = false;
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan);
    } catch (const std::exception &error) {
      rejected = std::string(error.what()).find("empty 0644") !=
                 std::string::npos;
    }
    if (!rejected || !SnapshotsEqual(before, Snapshot(fixture.install)) ||
        fs::exists(fixture.install / ".worr-update"))
      Fail("transaction accepted a non-empty stable runtime guard");
  }

#if !defined(_WIN32)
  {
    Fixture fixture("runtime-guard-mode");
    SetMode(fixture.install / "worr_install.lock",
            fs::perms::owner_read | fs::perms::owner_write);
    bool rejected = false;
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan);
    } catch (const std::exception &error) {
      rejected = std::string(error.what()).find("empty 0644") !=
                 std::string::npos;
    }
    if (!rejected || fs::exists(fixture.install / ".worr-update"))
      Fail("transaction accepted a stable runtime guard with mode 0600");
  }
#endif

  {
    Fixture fixture("runtime-guard-recovery-content");
    const InstallSnapshot before = Snapshot(fixture.install);
    InstallTransactionOptions options;
    options.hook = [](InstallTransactionPhase phase,
                      InstallTransactionCheckpoint checkpoint,
                      std::string_view path) {
      if (phase == InstallTransactionPhase::Remove &&
          checkpoint == InstallTransactionCheckpoint::AfterRemove &&
          path == "obsolete/remove.dat")
        throw SimulatedInstallTransactionCrash("retain recovery journal");
    };
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.plan,
                              options);
    } catch (const SimulatedInstallTransactionCrash &) {
    }
    WriteFile(fixture.install / "worr_install.lock", "not-empty");
    bool rejected = false;
    try {
      (void)RecoverInstallTransaction(fixture.install);
    } catch (const std::exception &error) {
      rejected = std::string(error.what()).find("empty 0644") !=
                 std::string::npos;
    }
    if (!rejected ||
        !fs::exists(fixture.install / ".worr-update" / "journal"))
      Fail("restart recovery accepted a malformed stable runtime guard");
    WriteFile(fixture.install / "worr_install.lock", "");
    if (RecoverInstallTransaction(fixture.install) !=
            InstallTransactionRecoveryResult::RolledBackInterruptedApply ||
        !SnapshotsEqual(before, Snapshot(fixture.install)))
      Fail("restart recovery could not resume after repairing its guard");
  }
}

void TestReservedStatePathRejected() {
  for (std::string_view reserved_path : {".WORR-UPDATE/journal",
                                         "WORR_INSTALL.LOCK"}) {
    Fixture fixture(reserved_path.front() == '.' ? "reserved-state"
                                                 : "reserved-runtime-lock");
    const InstallSnapshot before = Snapshot(fixture.install);
    fixture.plan.remove_paths.push_back(std::string(reserved_path));
    bool rejected = false;
    try {
      ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                              fixture.plan);
    } catch (const std::exception &) {
      rejected = true;
    }
    if (!rejected || !SnapshotsEqual(before, Snapshot(fixture.install)) ||
        fs::exists(fixture.install / ".worr-update"))
      Fail("reserved transaction path was not rejected pre-mutation");
  }
}

void TestOversizedJournalRejectedBeforeMutation() {
  Fixture fixture("oversized-journal");
  const InstallSnapshot before = Snapshot(fixture.install);
  fixture.plan.install_files.clear();
  fixture.plan.remove_paths.clear();
  const std::string long_component(220, 'x');
  for (int index = 0; index < 40000; ++index) {
    fixture.plan.remove_paths.push_back("oversized/" + std::to_string(index) +
                                        "/" + long_component);
  }

  bool rejected = false;
  try {
    ApplyInstallTransaction(fixture.stage, fixture.install, fixture.rollback,
                            fixture.plan);
  } catch (const std::exception &) {
    rejected = true;
  }
  if (!rejected || !SnapshotsEqual(before, Snapshot(fixture.install)) ||
      fs::exists(fixture.install / ".worr-update"))
    Fail("oversized journal was not rejected before transaction mutation");
}

void TestRecoveryNoOpDoesNotRequireWrites() {
  Fixture fixture("readonly-no-recovery");
#if !defined(_WIN32)
  SetMode(fixture.install, fs::perms::owner_read | fs::perms::owner_exec |
                               fs::perms::group_read | fs::perms::group_exec |
                               fs::perms::others_read | fs::perms::others_exec);
#endif
  const InstallTransactionRecoveryResult result =
      RecoverInstallTransaction(fixture.install);
#if !defined(_WIN32)
  SetMode(fixture.install, fs::perms::owner_all);
#endif
  if (result != InstallTransactionRecoveryResult::NothingToRecover ||
      fs::exists(fixture.install / ".worr-update"))
    Fail("empty startup recovery mutated a journal-free install");
}

} // namespace

int main() {
  try {
    TestSuccess();
    TestApplyFaults();
    TestProgressInterruption();
    TestCopiedBytesAuthenticated();
    TestRollbackOrder();
    TestIncompleteRollbackError();
    TestManifestCreationRollback();
    TestDuplicatePathRejectedBeforeMutation();
    TestInterruptedApplyRestartRecovery();
    TestRecoveryCanResumeAfterSecondInterruption();
    TestLegacyV2JournalRecovery();
    TestCommittedMarkerNeverRollsBack();
    TestPrivatePersistentStatePermissions();
    TestShapeTransitionSuccess();
    TestShapeTransitionRollback();
    TestShapeTransitionRestartRecoveryAndRuntimeGuard();
    TestShapeTransitionPreflightRejectsUnownedContents();
    TestRuntimeGuardRejectsConcurrentApply();
    TestRuntimeGuardMetadataValidation();
    TestReservedStatePathRejected();
    TestOversizedJournalRejectedBeforeMutation();
    TestRecoveryNoOpDoesNotRequireWrites();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "unexpected install transaction test error: %s\n",
                 error.what());
    return 1;
  } catch (...) {
    std::fprintf(stderr,
                 "unexpected non-standard install transaction test error\n");
    return 1;
  }
  return failures == 0 ? 0 : 1;
}
