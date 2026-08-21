#include "install_transaction.h"

#include "archive_path.h"
#include "install_root.h"

#include <algorithm>
#include <charconv>
#include <exception>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace worr::updater {

namespace {

constexpr std::string_view kStateDirectory = ".worr-update";
constexpr std::string_view kRollbackDirectory = ".worr-update/rollback";
constexpr std::string_view kLockPath = ".worr-update/lock";
constexpr std::string_view kJournalPath = ".worr-update/journal";
constexpr std::string_view kRuntimeLockPath = "worr_install.lock";
constexpr std::string_view kEmptySha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr std::string_view kJournalHeader = "WORR_INSTALL_TRANSACTION_V3";
constexpr std::string_view kLegacyJournalHeader =
    "WORR_INSTALL_TRANSACTION_V2";
// Each published manifest is producer-limited to half of these transition
// bounds. This leaves enough room for a fully disjoint authenticated
// predecessor: every old path may be removed while every new path is added.
constexpr std::size_t kMaximumJournalSize = 16 * 1024 * 1024;
constexpr std::size_t kMaximumJournalEntries = 200000;
constexpr InstallFileWriteOptions kPrivateStateWrite{0600, false};

enum class JournalState { Applying, Committed, RolledBack };

enum class OriginalPathKind { Absent, RegularFile, Directory };

struct OriginalFileState {
  std::string path;
  OriginalPathKind kind = OriginalPathKind::Absent;
  bool backup_ready = false;
  std::string backup_path;
  uint32_t mode = 0;
};

struct TransactionJournal {
  JournalState state = JournalState::Applying;
  std::vector<OriginalFileState> original_files;
};

InstallFileExpectation ParseExpectation(const InstallTransactionFile &file) {
  try {
    return MakeInstallFileExpectation(file.size, file.sha256, file.mode);
  } catch (const std::exception &) {
    throw std::runtime_error(
        "Install transaction contains an invalid SHA-256 for " + file.path);
  }
}

bool IsReservedTransactionPath(std::string_view path) {
  const std::string key = UpdaterPathCollisionKey(path);
  return key == kRuntimeLockPath || key == kStateDirectory ||
         (key.size() > kStateDirectory.size() &&
          key.compare(0, kStateDirectory.size(), kStateDirectory) == 0 &&
          key[kStateDirectory.size()] == '/');
}

void ValidatePlan(const InstallTransactionPlan &plan) {
  if (plan.install_files.size() > kMaximumJournalEntries ||
      plan.remove_paths.size() >
          kMaximumJournalEntries - plan.install_files.size() ||
      plan.install_files.size() + plan.remove_paths.size() >=
          kMaximumJournalEntries)
    throw std::runtime_error(
        "Install transaction contains too many journaled paths");
  std::map<std::string, std::string> collision_keys;
  std::vector<std::pair<std::string, std::string>> final_paths;
  std::vector<std::pair<std::string, std::string>> removal_paths;
  // Preflight the worst-case journal form before any transaction state or
  // managed file is created. Hex encoding doubles path bytes; 64 bytes covers
  // the entry fields and longest fixed rollback path.
  std::size_t maximum_journal_size = 128;
  const auto record_path = [&collision_keys,
                            &maximum_journal_size](std::string_view path) {
    if (!IsSafeUpdaterRelativePath(path))
      throw std::runtime_error("Unsafe install transaction path: " +
                               std::string(path));
    if (IsReservedTransactionPath(path))
      throw std::runtime_error("Install transaction targets reserved state: " +
                               std::string(path));
    const std::string key = UpdaterPathCollisionKey(path);
    const auto [existing, inserted] =
        collision_keys.emplace(key, std::string(path));
    if (!inserted)
      throw std::runtime_error(
          "Install transaction contains duplicate or colliding paths: " +
          existing->second + " and " + std::string(path));
    if (maximum_journal_size > kMaximumJournalSize - 64 ||
        path.size() > (kMaximumJournalSize - maximum_journal_size - 64) / 2)
      throw std::runtime_error(
          "Install transaction journal would exceed its maximum size");
    maximum_journal_size += 64 + path.size() * 2;
  };

  for (const InstallTransactionFile &file : plan.install_files) {
    record_path(file.path);
    (void)ParseExpectation(file);
    final_paths.emplace_back(UpdaterPathCollisionKey(file.path), file.path);
  }
  for (const std::string &path : plan.remove_paths) {
    record_path(path);
    removal_paths.emplace_back(UpdaterPathCollisionKey(path), path);
  }
  record_path(plan.manifest_path);
  final_paths.emplace_back(UpdaterPathCollisionKey(plan.manifest_path),
                           plan.manifest_path);
  if (!plan.predecessor_manifest_sha256.empty())
    (void)MakeInstallFileExpectation(plan.predecessor_manifest_size,
                                     plan.predecessor_manifest_sha256);

  const auto reject_prefix_collisions = [](auto paths,
                                           std::string_view description) {
    std::map<std::string, std::string> by_key;
    for (const auto &path : paths)
      by_key.emplace(path.first, path.second);
    for (const auto &path : paths) {
      for (std::size_t separator = path.first.find('/');
           separator != std::string::npos;
           separator = path.first.find('/', separator + 1)) {
        const auto parent = by_key.find(path.first.substr(0, separator));
        if (parent != by_key.end())
          throw std::runtime_error(
              "Install transaction contains prefix-colliding " +
              std::string(description) + " paths: " + parent->second +
              " and " + path.second);
      }
    }
  };
  reject_prefix_collisions(final_paths, "final");
  reject_prefix_collisions(removal_paths, "removal");
}

InstallTransactionCheckpoint
MapFileCheckpoint(InstallFileOperationCheckpoint checkpoint) {
  switch (checkpoint) {
  case InstallFileOperationCheckpoint::AfterTemporaryFileCreated:
    return InstallTransactionCheckpoint::AfterTemporaryFileCreated;
  case InstallFileOperationCheckpoint::AfterTemporaryFileWritten:
    return InstallTransactionCheckpoint::AfterTemporaryFileWritten;
  case InstallFileOperationCheckpoint::BeforeAtomicReplacement:
    return InstallTransactionCheckpoint::BeforeAtomicReplacement;
  case InstallFileOperationCheckpoint::BeforeRemoval:
    return InstallTransactionCheckpoint::BeforeRemoval;
  case InstallFileOperationCheckpoint::AfterParentDirectoryFlushed:
    return InstallTransactionCheckpoint::AfterParentDirectoryFlushed;
  }
  throw std::logic_error("Unknown install file operation checkpoint");
}

std::string DescribeException(const std::exception_ptr &failure) {
  try {
    std::rethrow_exception(failure);
  } catch (const std::exception &error) {
    return error.what();
  } catch (...) {
    return "unknown non-standard exception";
  }
}

std::string JoinFailures(const std::vector<std::string> &failures) {
  std::ostringstream message;
  for (std::size_t index = 0; index < failures.size(); ++index) {
    if (index != 0)
      message << "; ";
    message << failures[index];
  }
  return message.str();
}

char HexDigit(unsigned value) {
  return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10);
}

unsigned ParseHexDigit(char value) {
  if (value >= '0' && value <= '9')
    return static_cast<unsigned>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<unsigned>(value - 'a' + 10);
  throw std::runtime_error("Install transaction journal contains invalid hex");
}

std::string HexEncode(std::string_view value) {
  std::string encoded;
  encoded.reserve(value.size() * 2);
  for (const unsigned char character : value) {
    encoded.push_back(HexDigit(character >> 4U));
    encoded.push_back(HexDigit(character & 0x0FU));
  }
  return encoded;
}

std::string HexDecode(std::string_view value) {
  if (value.empty() || value.size() % 2 != 0)
    throw std::runtime_error(
        "Install transaction journal contains invalid path encoding");
  std::string decoded;
  decoded.reserve(value.size() / 2);
  for (std::size_t index = 0; index < value.size(); index += 2) {
    decoded.push_back(static_cast<char>((ParseHexDigit(value[index]) << 4U) |
                                        ParseHexDigit(value[index + 1])));
  }
  if (!IsSafeUpdaterRelativePath(decoded) || IsReservedTransactionPath(decoded))
    throw std::runtime_error(
        "Install transaction journal contains an unsafe destination path");
  return decoded;
}

std::string BackupPath(std::size_t index) {
  std::ostringstream path;
  path << kRollbackDirectory << '/' << std::setfill('0') << std::setw(8)
       << index << ".bak";
  return path.str();
}

std::string JournalStateName(JournalState state) {
  switch (state) {
  case JournalState::Applying:
    return "applying";
  case JournalState::Committed:
    return "committed";
  case JournalState::RolledBack:
    return "rolled-back";
  }
  throw std::logic_error("Unknown install transaction journal state");
}

JournalState ParseJournalState(std::string_view state) {
  if (state == "applying")
    return JournalState::Applying;
  if (state == "committed")
    return JournalState::Committed;
  if (state == "rolled-back")
    return JournalState::RolledBack;
  throw std::runtime_error(
      "Install transaction journal contains an unknown state");
}

char OriginalKindName(OriginalPathKind kind) {
  switch (kind) {
  case OriginalPathKind::Absent:
    return 'a';
  case OriginalPathKind::RegularFile:
    return 'f';
  case OriginalPathKind::Directory:
    return 'd';
  }
  throw std::logic_error("Unknown original install path kind");
}

OriginalPathKind ParseOriginalKind(std::string_view kind) {
  if (kind == "a")
    return OriginalPathKind::Absent;
  if (kind == "f")
    return OriginalPathKind::RegularFile;
  if (kind == "d")
    return OriginalPathKind::Directory;
  throw std::runtime_error(
      "Install transaction journal contains an invalid path kind");
}

std::string SerializeJournal(const TransactionJournal &journal) {
  std::ostringstream output;
  output << kJournalHeader << '\n';
  output << "state " << JournalStateName(journal.state) << '\n';
  for (std::size_t index = 0; index < journal.original_files.size(); ++index) {
    const OriginalFileState &original = journal.original_files[index];
    output << "entry " << OriginalKindName(original.kind) << ' '
           << (original.backup_ready ? '1' : '0') << ' '
           << (original.kind == OriginalPathKind::RegularFile
                   ? original.backup_path
                   : "-")
           << ' '
           << original.mode << ' '
           << HexEncode(original.path) << '\n';
  }
  return output.str();
}

TransactionJournal ParseJournal(const std::string &contents) {
  std::istringstream input(contents);
  std::string line;
  if (!std::getline(input, line) ||
      (line != kJournalHeader && line != kLegacyJournalHeader))
    throw std::runtime_error("Install transaction journal header is invalid");
  const bool legacy = line == kLegacyJournalHeader;
  if (!std::getline(input, line) || line.rfind("state ", 0) != 0)
    throw std::runtime_error("Install transaction journal state is missing");

  TransactionJournal journal;
  journal.state = ParseJournalState(std::string_view(line).substr(6));
  std::set<std::string> collision_keys;
  while (std::getline(input, line)) {
    if (line.empty())
      throw std::runtime_error(
          "Install transaction journal contains an empty record");
    std::istringstream record(line);
    std::string kind;
    std::string kind_value;
    std::string backup_ready_value;
    std::string backup_path;
    std::string mode_value;
    std::string encoded_path;
    std::string unexpected;
    if (!(record >> kind >> kind_value >> backup_ready_value >>
          backup_path >> mode_value >> encoded_path) ||
        record >> unexpected || kind != "entry" ||
        (backup_ready_value != "0" && backup_ready_value != "1"))
      throw std::runtime_error(
          "Install transaction journal contains an invalid record");
    if (journal.original_files.size() >= kMaximumJournalEntries)
      throw std::runtime_error(
          "Install transaction journal has too many entries");

    OriginalFileState original;
    original.path = HexDecode(encoded_path);
    if (legacy) {
      if (kind_value != "0" && kind_value != "1")
        throw std::runtime_error(
            "Install transaction journal contains an invalid record");
      original.kind = kind_value == "1" ? OriginalPathKind::RegularFile
                                          : OriginalPathKind::Absent;
    } else {
      original.kind = ParseOriginalKind(kind_value);
    }
    original.backup_ready = backup_ready_value == "1";
    original.backup_path =
        original.kind == OriginalPathKind::RegularFile ? backup_path
                                                        : std::string{};
    const char *mode_begin = mode_value.data();
    const char *mode_end = mode_begin + mode_value.size();
    const auto mode_result =
        std::from_chars(mode_begin, mode_end, original.mode, 10);
    if (mode_result.ec != std::errc{} || mode_result.ptr != mode_end ||
        (original.mode & ~uint32_t{0777}) != 0 ||
        (original.kind == OriginalPathKind::Absent && original.mode != 0))
      throw std::runtime_error(
          "Install transaction journal contains an invalid file mode");
    const std::string expected_backup =
        BackupPath(journal.original_files.size());
    if ((original.kind == OriginalPathKind::RegularFile &&
         original.backup_path != expected_backup) ||
        (original.kind != OriginalPathKind::RegularFile &&
         backup_path != "-"))
      throw std::runtime_error(
          "Install transaction journal contains an invalid backup path");
    if (original.kind != OriginalPathKind::RegularFile &&
        !original.backup_ready)
      throw std::runtime_error(
          "Install transaction journal marks an absent file as unprepared");
    if (!collision_keys.insert(UpdaterPathCollisionKey(original.path)).second)
      throw std::runtime_error(
          "Install transaction journal contains colliding destinations");
    journal.original_files.push_back(std::move(original));
  }
  return journal;
}

void Notify(const InstallTransactionOptions &options,
            InstallTransactionPhase phase,
            InstallTransactionCheckpoint checkpoint, std::string_view path) {
  if (options.hook)
    options.hook(phase, checkpoint, path);
}

void PersistJournal(AnchoredFileTree &installed_files,
                    const TransactionJournal &journal,
                    const InstallTransactionOptions &options,
                    InstallTransactionPhase phase,
                    InstallTransactionCheckpoint checkpoint) {
  const std::string contents = SerializeJournal(journal);
  if (contents.size() > kMaximumJournalSize)
    throw std::runtime_error(
        "Install transaction journal exceeds its maximum size");
  installed_files.ReplaceFileWithBytes(kJournalPath, contents,
                                       kPrivateStateWrite);
  Notify(options, phase, checkpoint, kJournalPath);
}

std::vector<std::string>
RollbackJournal(AnchoredFileTree &installed_files,
                const TransactionJournal &journal,
                const InstallTransactionOptions &options) {
  std::vector<std::string> failures;
  for (auto original = journal.original_files.rbegin();
       original != journal.original_files.rend(); ++original) {
    if (!original->backup_ready)
      continue;
    try {
      Notify(options, InstallTransactionPhase::Rollback,
             InstallTransactionCheckpoint::DuringRollback, original->path);
      const InstallPathKind current =
          installed_files.GetPathKind(original->path);
      if (original->kind == OriginalPathKind::RegularFile) {
        if (current == InstallPathKind::Directory)
          (void)installed_files.RemoveEmptyDirectory(original->path);
        const uint32_t destination_mode = original->mode;
        if (!installed_files.ReplaceFileFrom(
                installed_files, original->backup_path, original->path,
                nullptr, nullptr, &destination_mode))
          throw std::runtime_error("rollback backup disappeared");
      } else if (original->kind == OriginalPathKind::Directory) {
        if (current == InstallPathKind::RegularFile)
          (void)installed_files.RemoveFile(original->path);
        (void)installed_files.EnsureDirectory(original->path, original->mode);
      } else if (current == InstallPathKind::RegularFile) {
        (void)installed_files.RemoveFile(original->path);
      } else if (current == InstallPathKind::Directory) {
        (void)installed_files.RemoveEmptyDirectory(original->path);
      }
    } catch (const SimulatedInstallTransactionCrash &) {
      throw;
    } catch (const std::exception &error) {
      failures.push_back(original->path + ": " + error.what());
    } catch (...) {
      failures.push_back(original->path + ": unknown non-standard exception");
    }
  }
  return failures;
}

void CleanupJournal(AnchoredFileTree &installed_files,
                    const TransactionJournal &journal,
                    const InstallTransactionOptions &options) {
  for (const OriginalFileState &original : journal.original_files) {
    if (original.kind != OriginalPathKind::RegularFile)
      continue;
    (void)installed_files.RemoveFile(original.backup_path);
    Notify(options, InstallTransactionPhase::Cleanup,
           InstallTransactionCheckpoint::DuringCleanup, original.backup_path);
  }
  (void)installed_files.RemoveFile(kJournalPath);
  Notify(options, InstallTransactionPhase::Cleanup,
         InstallTransactionCheckpoint::DuringCleanup, kJournalPath);
}

InstallTransactionRecoveryResult
RecoverLocked(AnchoredFileTree &installed_files,
              const InstallTransactionOptions &options) {
  if (!installed_files.HasRegularFile(kJournalPath))
    return InstallTransactionRecoveryResult::NothingToRecover;
  TransactionJournal journal =
      ParseJournal(installed_files.ReadFile(kJournalPath, kMaximumJournalSize));
  if (journal.state == JournalState::Committed) {
    CleanupJournal(installed_files, journal, options);
    return InstallTransactionRecoveryResult::CleanedCommittedTransaction;
  }
  if (journal.state == JournalState::RolledBack) {
    CleanupJournal(installed_files, journal, options);
    return InstallTransactionRecoveryResult::CleanedRolledBackTransaction;
  }

  const std::vector<std::string> failures =
      RollbackJournal(installed_files, journal, options);
  if (!failures.empty())
    throw std::runtime_error(
        "Interrupted update recovery was incomplete; rollback errors: " +
        JoinFailures(failures));
  journal.state = JournalState::RolledBack;
  PersistJournal(installed_files, journal, options,
                 InstallTransactionPhase::Rollback,
                 InstallTransactionCheckpoint::AfterRollbackMarker);
  CleanupJournal(installed_files, journal, options);
  return InstallTransactionRecoveryResult::RolledBackInterruptedApply;
}

void PrepareTransactionState(AnchoredFileTree &installed_files) {
  installed_files.EnsurePrivateDirectory(kRollbackDirectory);
}

void ValidateRuntimeLockFile(AnchoredFileTree &installed_files) {
  const InstallFileExpectation expected =
      MakeInstallFileExpectation(0, kEmptySha256, 0644);
  if (!installed_files.FileMatches(kRuntimeLockPath, expected))
    throw std::runtime_error(
        "Stable updater runtime lock must be an empty 0644 regular file");
}

struct ObservedPathState {
  InstallPathKind kind = InstallPathKind::Missing;
  uint32_t mode = 0;
};

struct DirectoryTransition {
  std::string path;
  ObservedPathState original;
};

struct TransactionPreflight {
  std::map<std::string, ObservedPathState> explicit_paths;
  std::vector<DirectoryTransition> remove_directories;
  std::vector<DirectoryTransition> create_directories;
};

std::vector<std::string> PathPrefixes(std::string_view path,
                                      bool include_leaf) {
  const std::string canonical = CanonicalUpdaterRelativePath(path);
  std::vector<std::string> prefixes;
  for (std::size_t separator = canonical.find('/');
       separator != std::string::npos;
       separator = canonical.find('/', separator + 1))
    prefixes.push_back(canonical.substr(0, separator));
  if (include_leaf)
    prefixes.push_back(canonical);
  return prefixes;
}

std::size_t PathDepth(std::string_view path) {
  return static_cast<std::size_t>(
             std::count(path.begin(), path.end(), '/')) +
         1;
}

TransactionPreflight PreflightTransaction(
    AnchoredFileTree &installed_files, const InstallTransactionPlan &plan) {
  TransactionPreflight result;
  std::map<std::string, std::string> removals;
  for (const std::string &path : plan.remove_paths)
    removals.emplace(UpdaterPathCollisionKey(path), path);

  const auto inspect_chain = [&installed_files](std::string_view path) {
    std::vector<std::pair<std::string, ObservedPathState>> states;
    bool reachable = true;
    for (const std::string &prefix : PathPrefixes(path, true)) {
      ObservedPathState state;
      if (reachable)
        state.kind = installed_files.GetPathKind(prefix, &state.mode);
      states.emplace_back(prefix, state);
      if (state.kind != InstallPathKind::Directory)
        reachable = false;
    }
    return states;
  };

  for (const std::string &path : plan.remove_paths) {
    const auto chain = inspect_chain(path);
    for (std::size_t index = 0; index + 1 < chain.size(); ++index) {
      if (chain[index].second.kind == InstallPathKind::RegularFile)
        throw std::runtime_error(
            "Install removal is hidden beneath an existing file: " + path);
    }
    const ObservedPathState state = chain.back().second;
    if (state.kind == InstallPathKind::Directory)
      throw std::runtime_error(
          "Install removal unexpectedly names a directory: " + path);
    result.explicit_paths.emplace(UpdaterPathCollisionKey(path), state);
  }

  std::vector<std::string> targets;
  targets.reserve(plan.install_files.size() + 1);
  for (const InstallTransactionFile &file : plan.install_files)
    targets.push_back(file.path);
  targets.push_back(plan.manifest_path);

  std::map<std::string, DirectoryTransition> directories_to_create;
  std::map<std::string, DirectoryTransition> directories_to_remove;
  for (const std::string &target : targets) {
    const auto chain = inspect_chain(target);
    for (std::size_t index = 0; index + 1 < chain.size(); ++index) {
      const auto &[prefix, state] = chain[index];
      const std::string key = UpdaterPathCollisionKey(prefix);
      if (state.kind == InstallPathKind::RegularFile) {
        if (removals.find(key) == removals.end())
          throw std::runtime_error(
              "Install target is blocked by an existing file not selected "
              "for removal: " + prefix + " -> " + target);
        directories_to_create.emplace(
            key, DirectoryTransition{prefix, state});
      } else if (state.kind == InstallPathKind::Missing) {
        directories_to_create.emplace(
            key, DirectoryTransition{prefix, state});
      }
    }

    const ObservedPathState target_state = chain.back().second;
    result.explicit_paths.emplace(UpdaterPathCollisionKey(target),
                                  target_state);
    if (target_state.kind != InstallPathKind::Directory)
      continue;

    struct PendingDirectory {
      std::string path;
      uint32_t mode = 0;
    };
    std::vector<PendingDirectory> pending = {{target, target_state.mode}};
    while (!pending.empty()) {
      PendingDirectory directory = std::move(pending.back());
      pending.pop_back();
      const std::vector<InstallDirectoryEntry> entries =
          installed_files.ListDirectory(directory.path,
                                        kMaximumJournalEntries);
      if (entries.empty())
        throw std::runtime_error(
            "Install target directory has no authenticated managed contents: " +
            directory.path);
      directories_to_remove.emplace(
          UpdaterPathCollisionKey(directory.path),
          DirectoryTransition{directory.path,
                              {InstallPathKind::Directory, directory.mode}});
      if (directories_to_remove.size() >= kMaximumJournalEntries)
        throw std::runtime_error(
            "Install transaction contains too many transition directories");
      for (const InstallDirectoryEntry &entry : entries) {
        const std::string child = directory.path + "/" + entry.name;
        if (entry.kind == InstallPathKind::Directory) {
          pending.push_back({child, entry.mode});
          continue;
        }
        if (removals.find(UpdaterPathCollisionKey(child)) == removals.end())
          throw std::runtime_error(
              "Install target directory contains an unowned file: " + child);
      }
    }
  }

  for (auto &[key, directory] : directories_to_remove)
    result.remove_directories.push_back(std::move(directory));
  std::sort(result.remove_directories.begin(),
            result.remove_directories.end(),
            [](const DirectoryTransition &left,
               const DirectoryTransition &right) {
              const std::size_t left_depth = PathDepth(left.path);
              const std::size_t right_depth = PathDepth(right.path);
              if (left_depth != right_depth)
                return left_depth > right_depth;
              return UpdaterPathCollisionKey(left.path) <
                     UpdaterPathCollisionKey(right.path);
            });

  for (auto &[key, directory] : directories_to_create)
    result.create_directories.push_back(std::move(directory));
  std::sort(result.create_directories.begin(),
            result.create_directories.end(),
            [](const DirectoryTransition &left,
               const DirectoryTransition &right) {
              const std::size_t left_depth = PathDepth(left.path);
              const std::size_t right_depth = PathDepth(right.path);
              if (left_depth != right_depth)
                return left_depth < right_depth;
              return UpdaterPathCollisionKey(left.path) <
                     UpdaterPathCollisionKey(right.path);
            });

  const std::size_t implicit_directories = result.remove_directories.size() +
                                           result.create_directories.size();
  if (implicit_directories > kMaximumJournalEntries ||
      plan.install_files.size() + plan.remove_paths.size() + 1 >
          kMaximumJournalEntries - implicit_directories)
    throw std::runtime_error(
        "Install transaction contains too many implicit directory changes");
  std::size_t maximum_journal_size = SerializeJournal({}).size();
  const auto account_path = [&maximum_journal_size](std::string_view path) {
    if (maximum_journal_size > kMaximumJournalSize - 64 ||
        path.size() > (kMaximumJournalSize - maximum_journal_size - 64) / 2)
      throw std::runtime_error(
          "Install transaction directory journal would exceed its maximum "
          "size");
    maximum_journal_size += 64 + path.size() * 2;
  };
  for (const InstallTransactionFile &file : plan.install_files)
    account_path(file.path);
  for (const std::string &path : plan.remove_paths)
    account_path(path);
  account_path(plan.manifest_path);
  for (const DirectoryTransition &directory : result.remove_directories)
    account_path(directory.path);
  for (const DirectoryTransition &directory : result.create_directories)
    account_path(directory.path);
  return result;
}

} // namespace

InstallTransactionRecoveryResult
RecoverInstallTransaction(const std::filesystem::path &install_root,
                          const InstallTransactionOptions &options) {
  AnchoredFileTree installed_files(std::filesystem::canonical(install_root));
  // Startup recovery must remain a genuinely read-only no-op for normal
  // installs, including media where the current user cannot write. Only an
  // existing journal authorizes creation/validation of recovery support state.
  if (!installed_files.HasRegularFile(kJournalPath))
    return InstallTransactionRecoveryResult::NothingToRecover;
  AnchoredFileLock runtime_lock =
      installed_files.AcquireExistingFileExclusiveLock(kRuntimeLockPath);
  ValidateRuntimeLockFile(installed_files);
  PrepareTransactionState(installed_files);
  AnchoredFileLock lock = installed_files.AcquireExclusiveLock(kLockPath);
  return RecoverLocked(installed_files, options);
}

void ApplyInstallTransaction(const std::filesystem::path &stage_root,
                             const std::filesystem::path &install_root,
                             const InstallTransactionPlan &plan,
                             const InstallTransactionOptions &options) {
  ValidatePlan(plan);

  InstallTransactionPhase phase = InstallTransactionPhase::Backup;
  std::string operation_display_path;
  const auto file_hook = [&phase, &operation_display_path,
                          &options](InstallFileOperationCheckpoint checkpoint,
                                    std::string_view path) {
    Notify(options, phase, MapFileCheckpoint(checkpoint),
           operation_display_path.empty()
               ? path
               : std::string_view(operation_display_path));
  };

  AnchoredFileTree staged_files(std::filesystem::canonical(stage_root));
  AnchoredFileTree installed_files(std::filesystem::canonical(install_root),
                                   file_hook);
  AnchoredFileLock runtime_lock =
      installed_files.AcquireExistingFileExclusiveLock(kRuntimeLockPath);
  ValidateRuntimeLockFile(installed_files);
  std::optional<AnchoredFileLock> transaction_lock;
  if (installed_files.HasRegularFile(kJournalPath)) {
    PrepareTransactionState(installed_files);
    transaction_lock.emplace(installed_files.AcquireExclusiveLock(kLockPath));
    (void)RecoverLocked(installed_files, options);
  }

  if (!plan.predecessor_manifest_sha256.empty()) {
    const InstallFileExpectation predecessor = MakeInstallFileExpectation(
        plan.predecessor_manifest_size, plan.predecessor_manifest_sha256);
    if (!installed_files.FileMatches(plan.manifest_path, predecessor))
      throw std::runtime_error(
          "Installed manifest changed while the update was pending; refusing "
          "to apply a stale transaction plan");
  }

  // Complete read-only validation before backing up or mutating managed files.
  for (const InstallTransactionFile &file : plan.install_files) {
    if (!staged_files.HasRegularFile(file.path))
      throw std::runtime_error("Missing staged file: " + file.path);
  }
  const TransactionPreflight preflight =
      PreflightTransaction(installed_files, plan);

  if (!transaction_lock) {
    PrepareTransactionState(installed_files);
    transaction_lock.emplace(installed_files.AcquireExclusiveLock(kLockPath));
  }

  TransactionJournal journal;
  PersistJournal(installed_files, journal, options,
                 InstallTransactionPhase::Backup,
                 InstallTransactionCheckpoint::AfterJournalPersisted);

  std::map<std::string, std::size_t> recorded_paths;
  const auto record_original = [&](const std::string &path,
                                   const ObservedPathState &state) {
    const std::string key = UpdaterPathCollisionKey(path);
    const auto existing = recorded_paths.find(key);
    if (existing != recorded_paths.end())
      return journal.original_files[existing->second].kind;
    phase = InstallTransactionPhase::Backup;
    OriginalFileState original;
    original.path = path;
    original.kind = state.kind == InstallPathKind::RegularFile
                        ? OriginalPathKind::RegularFile
                    : state.kind == InstallPathKind::Directory
                        ? OriginalPathKind::Directory
                        : OriginalPathKind::Absent;
    original.backup_ready = original.kind != OriginalPathKind::RegularFile;
    if (original.kind == OriginalPathKind::RegularFile)
      original.backup_path = BackupPath(journal.original_files.size());
    original.mode = state.mode;
    recorded_paths.emplace(key, journal.original_files.size());
    journal.original_files.push_back(original);
    PersistJournal(installed_files, journal, options, phase,
                   InstallTransactionCheckpoint::AfterJournalPersisted);
    if (original.kind == OriginalPathKind::RegularFile) {
      bool copied = false;
      operation_display_path = path;
      try {
        copied = installed_files.ReplaceFileFrom(installed_files, path,
                                                 original.backup_path, nullptr,
                                                 &journal.original_files.back()
                                                      .mode);
      } catch (...) {
        operation_display_path.clear();
        throw;
      }
      operation_display_path.clear();
      if (!copied)
        throw std::runtime_error(
            "Install destination changed before durable backup: " + path);
      journal.original_files.back().backup_ready = true;
      PersistJournal(installed_files, journal, options, phase,
                     InstallTransactionCheckpoint::AfterJournalPersisted);
    }
    Notify(options, phase, InstallTransactionCheckpoint::AfterBackup, path);
    return original.kind;
  };

  std::exception_ptr apply_failure;
  try {
    const std::size_t total_steps =
        plan.install_files.size() + plan.remove_paths.size();
    std::size_t completed_steps = 0;

    for (const std::string &path : plan.remove_paths) {
      const ObservedPathState state =
          preflight.explicit_paths.at(UpdaterPathCollisionKey(path));
      if (state.kind == InstallPathKind::RegularFile) {
        if (record_original(path, state) != OriginalPathKind::RegularFile)
          throw std::runtime_error("Install removal changed before backup: " +
                                   path);
        phase = InstallTransactionPhase::Remove;
        if (!installed_files.RemoveFile(path))
          throw std::runtime_error("Install removal changed during apply: " +
                                   path);
        Notify(options, phase, InstallTransactionCheckpoint::AfterRemove, path);
      }
      ++completed_steps;
      if (options.progress)
        options.progress(completed_steps, total_steps);
    }

    for (const DirectoryTransition &directory :
         preflight.remove_directories) {
      record_original(directory.path, directory.original);
      phase = InstallTransactionPhase::Remove;
      if (!installed_files.RemoveEmptyDirectory(directory.path))
        throw std::runtime_error(
            "Install directory changed during transition removal: " +
            directory.path);
      Notify(options, phase, InstallTransactionCheckpoint::AfterRemove,
             directory.path);
    }

    for (const DirectoryTransition &directory :
         preflight.create_directories) {
      record_original(directory.path, directory.original);
      phase = InstallTransactionPhase::Install;
      if (!installed_files.EnsureDirectory(directory.path, 0755))
        throw std::runtime_error(
            "Install directory changed before transition creation: " +
            directory.path);
      Notify(options, phase, InstallTransactionCheckpoint::AfterInstall,
             directory.path);
    }

    for (const InstallTransactionFile &file : plan.install_files) {
      const ObservedPathState state =
          preflight.explicit_paths.at(UpdaterPathCollisionKey(file.path));
      record_original(file.path, state);
      phase = InstallTransactionPhase::Install;
      const InstallFileExpectation expectation = ParseExpectation(file);
      if (!installed_files.ReplaceFileFrom(staged_files, file.path,
                                           &expectation))
        throw std::runtime_error("Staged file disappeared during install: " +
                                 file.path);
      Notify(options, phase, InstallTransactionCheckpoint::AfterInstall,
             file.path);
      ++completed_steps;
      if (options.progress)
        options.progress(completed_steps, total_steps);
    }

    const ObservedPathState manifest_state =
        preflight.explicit_paths.at(
            UpdaterPathCollisionKey(plan.manifest_path));
    record_original(plan.manifest_path, manifest_state);
    phase = InstallTransactionPhase::Manifest;
    installed_files.ReplaceFileWithBytes(plan.manifest_path,
                                         plan.manifest_contents,
                                         InstallFileWriteOptions{0644, false});
    Notify(options, phase, InstallTransactionCheckpoint::AfterManifest,
           plan.manifest_path);

    phase = InstallTransactionPhase::Commit;
    journal.state = JournalState::Committed;
    PersistJournal(installed_files, journal, options, phase,
                   InstallTransactionCheckpoint::AfterCommitMarker);
  } catch (const SimulatedInstallTransactionCrash &) {
    throw;
  } catch (...) {
    apply_failure = std::current_exception();
  }

  if (!apply_failure) {
    phase = InstallTransactionPhase::Cleanup;
    try {
      CleanupJournal(installed_files, journal, options);
    } catch (const std::exception &error) {
      throw std::runtime_error(
          "Update committed, but durable rollback cleanup was incomplete: " +
          std::string(error.what()));
    }
    return;
  }

  phase = InstallTransactionPhase::Rollback;
  const std::vector<std::string> rollback_failures =
      RollbackJournal(installed_files, journal, options);
  if (!rollback_failures.empty()) {
    throw std::runtime_error(
        "Update apply failed and rollback was incomplete; durable recovery "
        "state was retained; apply error: " +
        DescribeException(apply_failure) +
        "; rollback errors: " + JoinFailures(rollback_failures));
  }

  journal.state = JournalState::RolledBack;
  PersistJournal(installed_files, journal, options,
                 InstallTransactionPhase::Rollback,
                 InstallTransactionCheckpoint::AfterRollbackMarker);
  try {
    phase = InstallTransactionPhase::Cleanup;
    CleanupJournal(installed_files, journal, options);
  } catch (const std::exception &error) {
    throw std::runtime_error(
        "Update apply failed; rollback completed but durable cleanup was "
        "incomplete; apply error: " +
        DescribeException(apply_failure) + "; cleanup error: " + error.what());
  }
  std::rethrow_exception(apply_failure);
}

void ApplyInstallTransaction(const std::filesystem::path &stage_root,
                             const std::filesystem::path &install_root,
                             const std::filesystem::path &rollback_root,
                             const InstallTransactionPlan &plan,
                             const InstallTransactionOptions &options) {
  (void)rollback_root;
  ApplyInstallTransaction(stage_root, install_root, plan, options);
}

} // namespace worr::updater
