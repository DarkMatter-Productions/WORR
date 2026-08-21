#include "zip_attributes.h"

#include <stdexcept>
#include <system_error>

namespace worr::updater {

namespace {

constexpr uint16_t kZipHostUnix = 3;
constexpr uint32_t kUnixFileTypeMask = 0170000;
constexpr uint32_t kUnixRegularFile = 0100000;
constexpr uint32_t kUnixDirectory = 0040000;
constexpr uint32_t kUnixExecuteMask = 0000111;

} // namespace

void ValidateUpdaterZipEntryAttributes(std::string_view path, bool is_directory,
                                       uint16_t version_made_by,
                                       uint32_t external_attributes) {
  const uint16_t host_system = static_cast<uint16_t>(version_made_by >> 8);
  if (host_system != kZipHostUnix)
    return;

  const uint32_t unix_mode = external_attributes >> 16;
  const uint32_t file_type = unix_mode & kUnixFileTypeMask;
  const uint32_t expected_type = is_directory ? kUnixDirectory : kUnixRegularFile;
  if (file_type != 0 && file_type != expected_type)
    throw std::runtime_error(
        "Update archive entry has a non-regular Unix file type: " +
        std::string(path));
  if ((unix_mode & 07000) != 0)
    throw std::runtime_error(
        "Update archive entry has privileged Unix permission bits: " +
        std::string(path));
}

uint32_t UpdaterZipEntryMode(bool is_directory, uint16_t version_made_by,
                             uint32_t external_attributes) {
  if (is_directory)
    return 0755;
  const uint16_t host_system = static_cast<uint16_t>(version_made_by >> 8);
  const uint32_t unix_mode = external_attributes >> 16;
  return host_system == kZipHostUnix && (unix_mode & kUnixExecuteMask) != 0
             ? 0755
             : 0644;
}

void ApplyUpdaterZipEntryAttributes(const std::filesystem::path &path, bool is_directory,
                                    uint16_t version_made_by, uint32_t external_attributes) {
  ValidateUpdaterZipEntryAttributes(path.generic_u8string(), is_directory,
                                    version_made_by, external_attributes);
#if !defined(_WIN32)
  std::error_code error;
  const auto permissions = static_cast<std::filesystem::perms>(
      UpdaterZipEntryMode(is_directory, version_made_by, external_attributes));
  std::filesystem::permissions(path, permissions,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::filesystem::filesystem_error(
        "Could not set deterministic extracted update permissions", path,
        error);
#else
  (void)path;
#endif
}

} // namespace worr::updater
