#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace worr::updater {

enum class InstallFileOperationCheckpoint {
  AfterTemporaryFileCreated,
  AfterTemporaryFileWritten,
  BeforeAtomicReplacement,
  BeforeRemoval,
  AfterParentDirectoryFlushed,
};

// Tests can throw from this hook to model interruption and filesystem errors
// at otherwise unreachable points in an atomic file operation. Production
// callers omit it, leaving no active fault-injection mechanism.
using InstallFileOperationHook =
    std::function<void(InstallFileOperationCheckpoint, std::string_view)>;

struct InstallFileExpectation {
  uint64_t size = 0;
  std::array<uint8_t, 32> sha256{};
  // Zero means byte-only validation (used for rollback backups). Published
  // payload expectations use a deterministic 0644 or 0755 mode on POSIX.
  uint32_t mode = 0;
};

// POSIX permission policy for byte-generated files. Shared state defaults to
// 0644 when first created and preserves an existing file's mode. Private state
// uses an explicit mode (normally 0600) even when replacing an existing file.
// Windows ignores the numeric mode and relies on the destination directory's
// ACL; atomic replacement does not claim to preserve the old file's ACL.
struct InstallFileWriteOptions {
  uint32_t mode = 0644;
  bool preserve_existing_mode = true;
};

enum class InstallPathKind {
  Missing,
  RegularFile,
  Directory,
};

struct InstallDirectoryEntry {
  std::string name;
  InstallPathKind kind = InstallPathKind::Missing;
  // POSIX permission bits for regular files and directories. Windows reports
  // zero because its updater metadata policy is ACL-based.
  uint32_t mode = 0;
};

class AnchoredFileLock {
public:
  ~AnchoredFileLock();
  AnchoredFileLock(AnchoredFileLock &&) noexcept;
  AnchoredFileLock &operator=(AnchoredFileLock &&) noexcept;

  AnchoredFileLock(const AnchoredFileLock &) = delete;
  AnchoredFileLock &operator=(const AnchoredFileLock &) = delete;

private:
  struct Impl;
  explicit AnchoredFileLock(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
  friend class AnchoredFileTree;
};

// Converts a canonical hexadecimal digest into the typed expectation used by
// anchored file operations. Invalid digests are rejected before any I/O.
InstallFileExpectation MakeInstallFileExpectation(uint64_t size,
                                                   std::string_view sha256,
                                                   uint32_t mode = 0);

// On Windows, independently validates that the canonical install root, its
// ancestors, and the selected worker cannot be replaced or rewritten by a
// non-administrative identity. This is required inside the elevated process;
// command-line approval flags are not capabilities. Other platforms reject
// this Windows-only elevation check.
void ValidateProtectedElevationBoundary(
    const std::filesystem::path &canonical_root,
    std::string_view worker_relative_path);

#if defined(_WIN32)
// Evaluates one allowed ACE using Windows file-object generic-right mappings.
// This predicate is exposed so the inherit-only elevation-boundary rule can
// have focused regression coverage without requiring an elevated test process.
bool ProtectedElevationAceGrantsDangerousAccess(
    uint32_t access_mask, uint8_t ace_flags, bool directory,
    uint32_t dangerous_for_object, bool validate_child_inheritance);
#endif

// Owns an open handle for an updater-controlled directory tree. All descendant
// operations are performed relative to that handle and open one component at a
// time without following links/reparse points. This prevents a validated
// absolute path from being redirected between validation and mutation.
class AnchoredFileTree {
public:
  explicit AnchoredFileTree(const std::filesystem::path &root,
                            InstallFileOperationHook operation_hook = {});
  ~AnchoredFileTree();

  AnchoredFileTree(AnchoredFileTree &&) noexcept;
  AnchoredFileTree &operator=(AnchoredFileTree &&) noexcept;

  AnchoredFileTree(const AnchoredFileTree &) = delete;
  AnchoredFileTree &operator=(const AnchoredFileTree &) = delete;

  // Returns false only when the leaf does not exist. Existing links, special
  // files, directories, and multiply-linked files are rejected.
  bool HasRegularFile(std::string_view relative_path) const;

  // Classifies one leaf through the anchored no-follow tree. Existing links,
  // special files, and multiply-linked regular files are rejected. POSIX mode
  // receives the leaf permission bits when supplied; Windows reports zero.
  InstallPathKind GetPathKind(std::string_view relative_path,
                              uint32_t *mode = nullptr) const;

  // Enumerates a directory through its anchored handle and returns entries in
  // deterministic collision-key order. Links and special entries are rejected
  // instead of being traversed or silently ignored.
  std::vector<InstallDirectoryEntry>
  ListDirectory(std::string_view relative_path,
                std::size_t maximum_entries) const;

  // Opens and hashes the file through the anchored tree, so the validation is
  // tied to the same no-follow handle instead of an absolute-path re-open.
  // Returns false when the leaf is absent or does not match.
  bool FileMatches(std::string_view relative_path,
                   const InstallFileExpectation &expectation) const;

  // Reads a bounded regular file through the anchored no-follow tree.
  std::string ReadFile(std::string_view relative_path,
                       std::size_t maximum_size) const;

  // Creates every component with private-directory semantics. On POSIX,
  // newly created components are 0700 and pre-existing components must already
  // be exactly 0700. Windows validates that every component is a non-reparse
  // directory; ACL inheritance remains platform-managed.
  void EnsurePrivateDirectory(std::string_view relative_path);

  // Ensures exactly one ordinary directory exists. Parents must already
  // exist. Returns true when the leaf was created and false when an existing
  // ordinary directory was retained. POSIX applies mode only on creation;
  // Windows ignores it and relies on inherited ACLs.
  bool EnsureDirectory(std::string_view relative_path, uint32_t mode = 0755);

  // Acquires a non-blocking, process-scoped exclusive lock on a private regular
  // file and retains it until the returned object is destroyed.
  AnchoredFileLock AcquireExclusiveLock(std::string_view relative_path);

  // Acquires a non-blocking advisory lock on an existing ordinary file. These
  // methods never create the file or alter its mode/ACL and are suitable for a
  // signed local-manifest runtime/update guard. Multiple shared locks may
  // coexist; an exclusive lock conflicts with either lock kind.
  AnchoredFileLock
  AcquireExistingFileSharedLock(std::string_view relative_path);
  AnchoredFileLock
  AcquireExistingFileExclusiveLock(std::string_view relative_path);

  // Atomically replaces relative_path with a byte-for-byte copy of the same
  // path in source. Returns false without changing the destination when the
  // source leaf does not exist. Parent directories are created without
  // following links. On POSIX, copied_source_mode reports the mode from the
  // same open source handle, while destination_mode can explicitly override
  // the installed mode. Private updater-state destinations remain 0600.
  bool ReplaceFileFrom(const AnchoredFileTree &source,
                       std::string_view relative_path,
                       const InstallFileExpectation *expectation = nullptr,
                       uint32_t *copied_source_mode = nullptr,
                       const uint32_t *destination_mode = nullptr);

  // Like ReplaceFileFrom, but permits distinct safe source and destination
  // paths. This is used for durable rollback backups inside the install tree.
  bool ReplaceFileFrom(const AnchoredFileTree &source,
                       std::string_view source_path,
                       std::string_view destination_path,
                       const InstallFileExpectation *expectation = nullptr,
                       uint32_t *copied_source_mode = nullptr,
                       const uint32_t *destination_mode = nullptr);

  // Atomically replaces a file with the supplied bytes.
  void ReplaceFileWithBytes(std::string_view destination_path,
                            const std::string &contents,
                            InstallFileWriteOptions options = {});

  // Removes a regular file without following the leaf or any ancestor. Returns
  // false when the leaf does not exist.
  bool RemoveFile(std::string_view relative_path);

  // Removes an empty ordinary directory through the anchored parent handle.
  // Returns false when the leaf does not exist. Links, special objects,
  // regular files, and non-empty directories are rejected.
  bool RemoveEmptyDirectory(std::string_view relative_path);

private:
  AnchoredFileLock AcquireExistingFileLock(std::string_view relative_path,
                                           bool exclusive);
  void NotifyOperation(InstallFileOperationCheckpoint checkpoint,
                       std::string_view relative_path) const;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace worr::updater
