#include "src/updater/archive_path.h"
#include "src/updater/zip_attributes.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using worr::updater::CanonicalUpdaterRelativePath;
using worr::updater::ApplyUpdaterZipEntryAttributes;
using worr::updater::IsSafeUpdaterRelativePath;
using worr::updater::ResolveUpdaterPathWithoutRedirects;
using worr::updater::UpdaterPathCollisionKey;

namespace {

int failures = 0;

void ExpectPath(std::string_view path, bool expected, bool allow_trailing_separator = false) {
  const bool actual = IsSafeUpdaterRelativePath(path, allow_trailing_separator);
  if (actual == expected)
    return;
  std::fprintf(stderr, "path safety mismatch for '%.*s': expected %d, got %d\n", static_cast<int>(path.size()),
               path.data(), expected ? 1 : 0, actual ? 1 : 0);
  ++failures;
}

void ExpectResolutionRejected(const std::filesystem::path &root, std::string_view path,
                              std::string_view failure_message) {
  try {
    ResolveUpdaterPathWithoutRedirects(root, path);
  } catch (const std::exception &) {
    return;
  }

  std::fprintf(stderr, "%.*s\n", static_cast<int>(failure_message.size()), failure_message.data());
  ++failures;
}

void ExpectZipAttributesRejected(const std::filesystem::path &path, uint32_t external_attributes,
                                 std::string_view failure_message) {
  try {
    ApplyUpdaterZipEntryAttributes(path, false, 3u << 8, external_attributes);
  } catch (const std::exception &) {
    return;
  }

  std::fprintf(stderr, "%.*s\n", static_cast<int>(failure_message.size()), failure_message.data());
  ++failures;
}

} // namespace

int RunTests() {
  ExpectPath("basew/pak0.pkz", true);
  ExpectPath("bin\\worr_engine_x86_64.dll", true);
  ExpectPath("licenses/WORR-LICENSE.txt", true);
  ExpectPath("ui/caf\xC3\xA9.rml", false);
  ExpectPath("ui/cafe\xCC\x81.rml", false);
  ExpectPath("basew/maps/", true, true);
  const std::string long_safe_path = std::string(200, 'a') + "/" + std::string(200, 'b') + "/" +
                                     std::string(200, 'c') + ".dat";
  ExpectPath(long_safe_path, true);

  ExpectPath("", false);
  ExpectPath("/absolute", false);
  ExpectPath("\\absolute", false);
  ExpectPath("C:/absolute", false);
  ExpectPath("C:relative", false);
  ExpectPath("../escape", false);
  ExpectPath("basew/../escape", false);
  ExpectPath("basew/./pak0.pkz", false);
  ExpectPath("basew//pak0.pkz", false);
  ExpectPath("basew\\..\\escape", false);
  ExpectPath("basew/pak0.pkz/", false);
  ExpectPath("basew//", false, true);
  ExpectPath("basew/NUL", false);
  ExpectPath("basew/con.txt", false);
  ExpectPath("basew/CONIN$", false);
  ExpectPath("basew/conout$.log", false);
  ExpectPath("basew/CLOCK$", false);
  ExpectPath(u8"basew/COM¹.txt", false);
  ExpectPath(u8"basew/LPT³.cfg", false);
  ExpectPath("basew/trailing.", false);
  ExpectPath("basew/trailing ", false);
  ExpectPath("basew/bad?.cfg", false);
  ExpectPath(std::string(256, 'a'), false);
  ExpectPath(std::string(4097, 'a'), false);
  ExpectPath(std::string("basew/") + "\xC0\xAF", false);

  if (CanonicalUpdaterRelativePath("bin\\renderer\\worr_vulkan.dll") != "bin/renderer/worr_vulkan.dll") {
    std::fprintf(stderr, "path separator canonicalization failed\n");
    ++failures;
  }
  if (UpdaterPathCollisionKey("BaseW\\Maps\\") != UpdaterPathCollisionKey("basew/maps")) {
    std::fprintf(stderr, "portable path collision key failed\n");
    ++failures;
  }

  namespace fs = std::filesystem;
  const fs::path test_root = fs::temp_directory_path() /
                             ("worr-updater-path-test-" +
                              std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  const fs::path install_root = test_root / "install";
  const fs::path outside_root = test_root / "outside";
  fs::create_directories(install_root / "safe");
  fs::create_directories(outside_root);
  const fs::path canonical_root = fs::canonical(install_root);

  if (ResolveUpdaterPathWithoutRedirects(canonical_root, "safe/new/file.dat") !=
      canonical_root / "safe" / "new" / "file.dat") {
    std::fprintf(stderr, "safe updater-owned path resolution failed\n");
    ++failures;
  }

  {
    std::ofstream blocker(install_root / "blocker", std::ios::binary);
    blocker << "not a directory";
  }
  ExpectResolutionRejected(canonical_root, "blocker/child.dat",
                           "non-directory updater path component was accepted");

  const fs::path hard_link_source = install_root / "hard-link-source.dat";
  {
    std::ofstream source(hard_link_source, std::ios::binary);
    source << "linked";
  }
  std::error_code hard_link_error;
  fs::create_hard_link(hard_link_source, install_root / "hard-link-alias.dat", hard_link_error);
  if (!hard_link_error)
    ExpectResolutionRejected(canonical_root, "hard-link-source.dat",
                             "hard-linked updater-owned file was accepted");

  std::error_code link_error;
  fs::create_directory_symlink(outside_root, install_root / "redirect", link_error);
  if (!link_error)
    ExpectResolutionRejected(canonical_root, "redirect/escaped.dat",
                             "redirecting updater path component was accepted");

  const fs::path executable_path = install_root / "executable-test.dat";
  {
    std::ofstream executable(executable_path, std::ios::binary);
    executable << "executable";
  }
  ApplyUpdaterZipEntryAttributes(executable_path, false, 3u << 8, 0100755u << 16);
#if !defined(_WIN32)
  if ((fs::status(executable_path).permissions() & fs::perms::owner_exec) == fs::perms::none) {
    std::fprintf(stderr, "Unix executable permission was not restored\n");
    ++failures;
  }
#endif
  ExpectZipAttributesRejected(executable_path, 0120777u << 16,
                              "Unix symbolic-link archive type was accepted");
  ExpectZipAttributesRejected(executable_path, 0010644u << 16,
                              "Unix special archive type was accepted");
  std::error_code cleanup_error;
  fs::remove_all(test_root, cleanup_error);

  return failures == 0 ? 0 : 1;
}

int main() {
  try {
    return RunTests();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "archive path test failed with an unexpected exception: %s\n", error.what());
  } catch (...) {
    std::fprintf(stderr, "archive path test failed with an unexpected non-standard exception\n");
  }
  return 1;
}
