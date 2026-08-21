#include "src/updater/install_root.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using worr::updater::AnchoredFileTree;
using worr::updater::InstallFileOperationCheckpoint;
using worr::updater::InstallFileWriteOptions;
using worr::updater::InstallPathKind;
using worr::updater::MakeInstallFileExpectation;
#if defined(_WIN32)
using worr::updater::ProtectedElevationAceGrantsDangerousAccess;
#endif

namespace {

int failures = 0;

void WriteFile(const std::filesystem::path &path, std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output)
    throw std::runtime_error("Could not write test fixture " + path.string());
}

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input)
    throw std::runtime_error("Could not read test fixture " + path.string());
  return contents.str();
}

template <typename Operation>
void ExpectRejected(Operation &&operation, std::string_view failure_message) {
  try {
    operation();
  } catch (const std::exception &) {
    return;
  }
  std::fprintf(stderr, "%.*s\n", static_cast<int>(failure_message.size()),
               failure_message.data());
  ++failures;
}

} // namespace

int RunTests() {
  namespace fs = std::filesystem;
  const fs::path test_root =
      fs::temp_directory_path() /
      ("worr-install-root-test-" +
       std::to_string(std::chrono::high_resolution_clock::now()
                          .time_since_epoch()
                          .count()));
  const fs::path source_root = test_root / "source";
  const fs::path destination_root = test_root / "destination";
  const fs::path outside_root = test_root / "outside";
  fs::create_directories(source_root);
  fs::create_directories(destination_root);
  fs::create_directories(outside_root);

#if defined(_WIN32)
  constexpr uint8_t inherited_only =
      OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE;
  constexpr uint32_t protected_directory_rights =
      FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD | DELETE |
      WRITE_DAC | WRITE_OWNER;
  if (!ProtectedElevationAceGrantsDangerousAccess(
          GENERIC_WRITE, inherited_only, true, protected_directory_rights,
          true) ||
      !ProtectedElevationAceGrantsDangerousAccess(
          FILE_GENERIC_WRITE, inherited_only, true,
          protected_directory_rights, true) ||
      ProtectedElevationAceGrantsDangerousAccess(
          GENERIC_READ, inherited_only, true, protected_directory_rights,
          true) ||
      ProtectedElevationAceGrantsDangerousAccess(
          GENERIC_WRITE, inherited_only, false, FILE_WRITE_DATA, true) ||
      ProtectedElevationAceGrantsDangerousAccess(
          GENERIC_WRITE, inherited_only, true, protected_directory_rights,
          false)) {
    std::fprintf(
        stderr,
        "protected elevation ACE propagation did not reject only dangerous "
        "directory inheritance\n");
    ++failures;
  }
#endif

  WriteFile(source_root / "runtime" / "engine.dat", "engine-v2");
#if !defined(_WIN32)
  fs::permissions(source_root / "runtime" / "engine.dat",
                  fs::perms::owner_read | fs::perms::owner_write |
                      fs::perms::owner_exec | fs::perms::group_read |
                      fs::perms::group_exec,
                  fs::perm_options::replace);
#endif

  {
    AnchoredFileTree source(source_root);
    AnchoredFileTree destination(destination_root);

    if (!destination.ReplaceFileFrom(source, "runtime/engine.dat") ||
        ReadFile(destination_root / "runtime" / "engine.dat") != "engine-v2") {
      std::fprintf(stderr, "anchored file copy failed\n");
      ++failures;
    }
#if !defined(_WIN32)
    const fs::perms copied_permissions =
        fs::status(destination_root / "runtime" / "engine.dat").permissions();
    if ((copied_permissions & fs::perms::owner_exec) == fs::perms::none) {
      std::fprintf(
          stderr,
          "anchored file copy did not preserve executable permissions\n");
      ++failures;
    }
#endif

    destination.ReplaceFileWithBytes("state/install.json", "{\"version\":2}\n");
    if (ReadFile(destination_root / "state" / "install.json") !=
        "{\"version\":2}\n") {
      std::fprintf(stderr, "anchored byte replacement failed\n");
      ++failures;
    }
    if (destination.ReadFile("state/install.json", 1024) !=
        "{\"version\":2}\n") {
      std::fprintf(stderr, "anchored bounded read returned wrong bytes\n");
      ++failures;
    }
    ExpectRejected(
        [&] { (void)destination.ReadFile("state/install.json", 2); },
        "bounded anchored read accepted an oversized file");

    destination.EnsurePrivateDirectory(".private/nested");
    if (!fs::is_directory(destination_root / ".private" / "nested")) {
      std::fprintf(stderr, "private anchored directory was not created\n");
      ++failures;
    }
#if !defined(_WIN32)
    constexpr fs::perms permission_mask =
        fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all;
    if ((fs::status(destination_root / ".private").permissions() &
         permission_mask) != fs::perms::owner_all ||
        (fs::status(destination_root / ".private" / "nested").permissions() &
         permission_mask) != fs::perms::owner_all) {
      std::fprintf(stderr, "private anchored directory mode was not 0700\n");
      ++failures;
    }

    fs::permissions(destination_root / "state" / "install.json",
                    fs::perms::owner_read | fs::perms::owner_write |
                        fs::perms::group_read | fs::perms::group_write,
                    fs::perm_options::replace);
    destination.ReplaceFileWithBytes("state/install.json", "shared\n");
    if ((fs::status(destination_root / "state" / "install.json").permissions() &
         permission_mask) !=
        (fs::perms::owner_read | fs::perms::owner_write |
         fs::perms::group_read | fs::perms::group_write)) {
      std::fprintf(stderr,
                   "byte replacement did not preserve an existing shared mode\n");
      ++failures;
    }
    destination.ReplaceFileWithBytes("state/private.json", "private\n",
                                     InstallFileWriteOptions{0600, false});
    if ((fs::status(destination_root / "state" / "private.json").permissions() &
         permission_mask) !=
        (fs::perms::owner_read | fs::perms::owner_write)) {
      std::fprintf(stderr, "explicit private byte replacement was not 0600\n");
      ++failures;
    }
#endif

    {
      auto first_lock = destination.AcquireExclusiveLock(".private/lock");
      ExpectRejected(
          [&] {
            auto second_lock =
                destination.AcquireExclusiveLock(".private/lock");
            (void)second_lock;
          },
          "concurrent anchored transaction lock was accepted");
    }

    {
      auto first_shared = destination.AcquireExistingFileSharedLock(
          "state/install.json");
      auto second_shared = destination.AcquireExistingFileSharedLock(
          "state/install.json");
      ExpectRejected(
          [&] {
            auto exclusive = destination.AcquireExistingFileExclusiveLock(
                "state/install.json");
            (void)exclusive;
          },
          "existing-file exclusive lock ignored active shared locks");
    }
    {
      auto exclusive = destination.AcquireExistingFileExclusiveLock(
          "state/install.json");
      ExpectRejected(
          [&] {
            auto shared = destination.AcquireExistingFileSharedLock(
                "state/install.json");
            (void)shared;
          },
          "existing-file shared lock ignored an active exclusive lock");
    }
    ExpectRejected(
        [&] {
          auto missing = destination.AcquireExistingFileSharedLock(
              "state/not-created.lock");
          (void)missing;
        },
        "existing-file lock created a missing lock file");
    if (fs::exists(destination_root / "state" / "not-created.lock")) {
      std::fprintf(stderr, "existing-file lock mutated a missing path\n");
      ++failures;
    }

    uint32_t path_mode = 0;
    if (destination.GetPathKind("state/install.json", &path_mode) !=
            InstallPathKind::RegularFile ||
        destination.GetPathKind("state", nullptr) !=
            InstallPathKind::Directory ||
        destination.GetPathKind("state/missing", nullptr) !=
            InstallPathKind::Missing) {
      std::fprintf(stderr, "anchored path-kind inspection failed\n");
      ++failures;
    }
#if !defined(_WIN32)
    if (path_mode != 0660) {
      std::fprintf(stderr, "existing-file locks changed the guarded mode\n");
      ++failures;
    }
#endif
    const auto state_entries = destination.ListDirectory("state", 16);
    const std::size_t expected_state_entries =
#if defined(_WIN32)
        1;
#else
        2;
#endif
    if (state_entries.size() != expected_state_entries ||
        state_entries[0].name != "install.json"
#if !defined(_WIN32)
        || state_entries[1].name != "private.json"
#endif
    ) {
      std::fprintf(stderr, "anchored directory listing was not deterministic\n");
      ++failures;
    }
    if (!destination.EnsureDirectory("state/ordinary", 0751) ||
        destination.EnsureDirectory("state/ordinary", 0700)) {
      std::fprintf(stderr, "anchored ordinary-directory creation was not exact\n");
      ++failures;
    }
#if !defined(_WIN32)
    if ((fs::status(destination_root / "state" / "ordinary").permissions() &
        permission_mask) !=
        (fs::perms::owner_all | fs::perms::group_read |
         fs::perms::group_exec | fs::perms::others_exec)) {
      std::fprintf(stderr,
                   "anchored ordinary-directory mode was not preserved\n");
      ++failures;
    }
#endif
    WriteFile(destination_root / "state" / "ordinary" / "child.dat",
              "child");
    ExpectRejected(
        [&] { (void)destination.RemoveEmptyDirectory("state/ordinary"); },
        "anchored empty-directory removal accepted a non-empty directory");
    fs::remove(destination_root / "state" / "ordinary" / "child.dat");
    if (!destination.RemoveEmptyDirectory("state/ordinary") ||
        destination.RemoveEmptyDirectory("state/ordinary")) {
      std::fprintf(stderr, "anchored empty-directory removal failed\n");
      ++failures;
    }

    if (!destination.ReplaceFileFrom(source, "runtime/engine.dat",
                                     "backup/engine-copy.dat") ||
        ReadFile(destination_root / "backup" / "engine-copy.dat") !=
            "engine-v2") {
      std::fprintf(stderr, "distinct-path anchored backup copy failed\n");
      ++failures;
    }

    if (!destination.HasRegularFile("runtime/engine.dat") ||
        destination.HasRegularFile("runtime/missing.dat")) {
      std::fprintf(stderr, "anchored regular-file inspection failed\n");
      ++failures;
    }
    const auto engine_expectation = MakeInstallFileExpectation(
        9, "0dac93a841f916b3dc9c2971ce82266463bcb225806dc78c8b97e5d1831a7f52");
    const auto wrong_size_expectation = MakeInstallFileExpectation(
        8, "0dac93a841f916b3dc9c2971ce82266463bcb225806dc78c8b97e5d1831a7f52");
    const auto wrong_hash_expectation = MakeInstallFileExpectation(
        9, "1dac93a841f916b3dc9c2971ce82266463bcb225806dc78c8b97e5d1831a7f52");
    if (!destination.FileMatches("runtime/engine.dat", engine_expectation) ||
        destination.FileMatches("runtime/engine.dat", wrong_size_expectation) ||
        destination.FileMatches("runtime/engine.dat", wrong_hash_expectation) ||
        destination.FileMatches("runtime/missing.dat", engine_expectation)) {
      std::fprintf(stderr, "anchored same-handle file validation failed\n");
      ++failures;
    }
    ExpectRejected(
        [] { (void)MakeInstallFileExpectation(1, "not-a-sha256"); },
        "invalid file expectation digest was accepted");

    const fs::path linked = destination_root / "runtime" / "linked.dat";
    WriteFile(linked, "linked");
    std::error_code hard_link_error;
    fs::create_hard_link(linked,
                         destination_root / "runtime" / "linked-alias.dat",
                         hard_link_error);
    if (!hard_link_error) {
      ExpectRejected(
          [&] {
            destination.ReplaceFileWithBytes("runtime/linked.dat",
                                             "replacement");
          },
          "hard-linked destination was accepted");
      ExpectRejected(
          [&] {
            (void)destination.FileMatches("runtime/linked.dat",
                                          engine_expectation);
          },
          "hard-linked source was accepted for validation");
    }

    std::error_code link_error;
    fs::create_directory_symlink(outside_root, destination_root / "redirect",
                                 link_error);
    if (!link_error) {
      ExpectRejected(
          [&] {
            destination.ReplaceFileWithBytes("redirect/escape.dat", "escape");
          },
          "redirecting destination ancestor was accepted");
      if (fs::exists(outside_root / "escape.dat")) {
        std::fprintf(stderr,
                     "anchored replacement escaped through a directory link\n");
        ++failures;
      }
    }

    WriteFile(outside_root / "source.dat", "outside-source");
    std::error_code source_link_error;
    fs::create_directory_symlink(outside_root, source_root / "source-redirect",
                                 source_link_error);
    if (!source_link_error) {
      ExpectRejected(
          [&] {
            destination.ReplaceFileFrom(source, "source-redirect/source.dat");
          },
          "redirecting source ancestor was accepted");
      if (fs::exists(destination_root / "source-redirect" / "source.dat") ||
          ReadFile(outside_root / "source.dat") != "outside-source") {
        std::fprintf(stderr,
                     "anchored copy escaped through a source directory link\n");
        ++failures;
      }
    }

    if (!destination.RemoveFile("runtime/engine.dat") ||
        destination.RemoveFile("runtime/engine.dat") ||
        fs::exists(destination_root / "runtime" / "engine.dat")) {
      std::fprintf(stderr, "anchored file removal failed\n");
      ++failures;
    }
  }

#if !defined(_WIN32)
  {
    WriteFile(destination_root / "race" / "victim.dat", "original");
    WriteFile(destination_root / "race" / "replacement.dat", "replacement");
    bool swapped = false;
    AnchoredFileTree destination(
        destination_root,
        [&](InstallFileOperationCheckpoint checkpoint, std::string_view path) {
          if (!swapped &&
              checkpoint == InstallFileOperationCheckpoint::BeforeRemoval &&
              path == "race/victim.dat") {
            fs::rename(destination_root / "race" / "replacement.dat",
                       destination_root / "race" / "victim.dat");
            swapped = true;
          }
        });
    ExpectRejected([&] { (void)destination.RemoveFile("race/victim.dat"); },
                   "concurrently exchanged removal leaf was deleted");
    if (!swapped ||
        ReadFile(destination_root / "race" / "victim.dat") != "replacement") {
      std::fprintf(stderr,
                   "concurrent removal defense did not preserve replacement\n");
      ++failures;
    }
  }

  {
    fs::create_directories(destination_root / "race" / "victim-dir");
    WriteFile(destination_root / "race" / "replacement-dir" / "marker.dat",
              "replacement");
    bool swapped = false;
    AnchoredFileTree destination(
        destination_root,
        [&](InstallFileOperationCheckpoint checkpoint, std::string_view path) {
          if (!swapped &&
              checkpoint == InstallFileOperationCheckpoint::BeforeRemoval &&
              path == "race/victim-dir") {
            fs::rename(destination_root / "race" / "replacement-dir",
                       destination_root / "race" / "victim-dir");
            swapped = true;
          }
        });
    ExpectRejected(
        [&] { (void)destination.RemoveEmptyDirectory("race/victim-dir"); },
        "concurrently exchanged directory leaf was deleted");
    if (!swapped ||
        ReadFile(destination_root / "race" / "victim-dir" / "marker.dat") !=
            "replacement") {
      std::fprintf(stderr,
                   "concurrent directory-removal defense did not preserve "
                   "replacement\n");
      ++failures;
    }
  }

  {
    std::size_t directory_flushes = 0;
    AnchoredFileTree destination(
        destination_root,
        [&](InstallFileOperationCheckpoint checkpoint, std::string_view) {
          if (checkpoint ==
              InstallFileOperationCheckpoint::AfterParentDirectoryFlushed)
            ++directory_flushes;
        });
    destination.ReplaceFileWithBytes("durability/proof.dat", "proof");
    (void)destination.RemoveFile("durability/proof.dat");
    if (directory_flushes != 2) {
      std::fprintf(stderr,
                   "POSIX replacement/removal did not report directory fsync\n");
      ++failures;
    }
  }
#endif

  // Once the tree is anchored, replacing its root pathname must not redirect
  // later mutations. Windows denies the rename because the held root handle
  // intentionally does not share delete access. POSIX continues through the
  // original directory descriptor after the pathname is renamed.
  const fs::path anchored_root = test_root / "anchored";
  const fs::path moved_root = test_root / "anchored-moved";
  fs::create_directories(anchored_root);
  {
    AnchoredFileTree anchored(anchored_root);
    std::error_code rename_error;
    fs::rename(anchored_root, moved_root, rename_error);
#if defined(_WIN32)
    if (!rename_error) {
      std::fprintf(stderr,
                   "Windows updater root could be renamed while anchored\n");
      ++failures;
    } else {
      anchored.ReplaceFileWithBytes("proof.dat", "anchored");
      if (ReadFile(anchored_root / "proof.dat") != "anchored") {
        std::fprintf(stderr, "Windows anchored root write failed\n");
        ++failures;
      }
    }
#else
    if (rename_error) {
      std::fprintf(stderr, "Could not rename POSIX updater root fixture: %s\n",
                   rename_error.message().c_str());
      ++failures;
    } else {
      fs::create_directories(anchored_root);
      std::error_code replacement_link_error;
      fs::create_directory_symlink(outside_root, anchored_root / "sub",
                                   replacement_link_error);
      if (replacement_link_error)
        throw std::runtime_error(
            "Could not create replacement-root redirect fixture");
      anchored.ReplaceFileWithBytes("sub/proof.dat", "anchored");
      if (ReadFile(moved_root / "sub" / "proof.dat") != "anchored" ||
          fs::exists(anchored_root / "sub" / "proof.dat") ||
          fs::exists(outside_root / "proof.dat")) {
        std::fprintf(
            stderr,
            "POSIX anchored root was redirected after pathname replacement\n");
        ++failures;
      }
    }
#endif
  }

  std::error_code cleanup_error;
  fs::remove_all(test_root, cleanup_error);
  return failures == 0 ? 0 : 1;
}

int main() {
  try {
    return RunTests();
  } catch (const std::exception &error) {
    std::fprintf(stderr,
                 "install root test failed with an unexpected exception: %s\n",
                 error.what());
  } catch (...) {
    std::fprintf(
        stderr,
        "install root test failed with an unexpected non-standard exception\n");
  }
  return 1;
}
