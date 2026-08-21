#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace worr::updater {

struct InstallTransactionFile {
  std::string path;
  uint64_t size = 0;
  std::string sha256;
  uint32_t mode = 0;
};

struct InstallTransactionPlan {
  std::vector<InstallTransactionFile> install_files;
  std::vector<std::string> remove_paths;
  std::string manifest_path;
  std::string manifest_contents;
  // Optional immutable predecessor binding. Updaters set this to the exact
  // authenticated local-manifest bytes used for discovery and prompting; the
  // transaction rechecks it while holding the exclusive install lock.
  uint64_t predecessor_manifest_size = 0;
  std::string predecessor_manifest_sha256;
};

enum class InstallTransactionPhase {
  Backup,
  Install,
  Remove,
  Manifest,
  Commit,
  Rollback,
  Cleanup,
};

enum class InstallTransactionCheckpoint {
  AfterTemporaryFileCreated,
  AfterTemporaryFileWritten,
  BeforeAtomicReplacement,
  BeforeRemoval,
  AfterBackup,
  AfterInstall,
  AfterRemove,
  AfterManifest,
  AfterJournalPersisted,
  AfterCommitMarker,
  AfterRollbackMarker,
  AfterParentDirectoryFlushed,
  DuringRollback,
  DuringCleanup,
};

// A test hook may throw to model an interruption or filesystem failure. The
// updater does not install a hook in production.
using InstallTransactionHook = std::function<void(
    InstallTransactionPhase, InstallTransactionCheckpoint, std::string_view)>;

// Called after each requested install or removal step. Throwing (for example,
// when the UI reports cancellation) aborts the apply and starts rollback.
using InstallTransactionProgress =
    std::function<void(std::size_t completed, std::size_t total)>;

struct InstallTransactionOptions {
  InstallTransactionHook hook;
  InstallTransactionProgress progress;
};

// Test hooks may throw this exception to model abrupt process termination.
// Unlike ordinary failures, ApplyInstallTransaction deliberately leaves its
// durable journal and rollback data intact for restart recovery.
class SimulatedInstallTransactionCrash : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class InstallTransactionRecoveryResult {
  NothingToRecover,
  RolledBackInterruptedApply,
  CleanedCommittedTransaction,
  CleanedRolledBackTransaction,
};

// Recovers the fixed private transaction state below install_root while
// holding an exclusive lock on the stable top-level worr_install.lock guard.
// An interrupted apply restores file bytes, directory topology, and, on POSIX,
// permission bits from durable state. Ownership, timestamps, ACLs, extended
// attributes, and other metadata are not claimed to be restored. A durable
// commit marker causes only cleanup; committed content is never rolled back.
InstallTransactionRecoveryResult RecoverInstallTransaction(
    const std::filesystem::path &install_root,
    const InstallTransactionOptions &options = {});

// Preferred API. Rollback state and the journal live beneath the fixed private
// .worr-update directory inside install_root, allowing deterministic recovery
// after an abrupt process interruption. File-to-directory and
// directory-to-file changes are preflighted and journaled in dependency order.
// POSIX mutations include file and parent-directory fsync barriers. Windows
// flushes file contents but does not claim recovery from sudden power loss
// because no directory/volume barrier is used.
void ApplyInstallTransaction(const std::filesystem::path &stage_root,
                             const std::filesystem::path &install_root,
                             const InstallTransactionPlan &plan,
                             const InstallTransactionOptions &options = {});

// Compatibility overload. rollback_root is no longer used: recoverable state
// must have a stable location, so it is persisted under install_root. Callers
// should migrate to the preferred overload and invoke recovery during startup.
void ApplyInstallTransaction(const std::filesystem::path &stage_root,
                             const std::filesystem::path &install_root,
                             const std::filesystem::path &rollback_root,
                             const InstallTransactionPlan &plan,
                             const InstallTransactionOptions &options = {});

} // namespace worr::updater
