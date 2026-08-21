#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace worr::updater {

// Update paths are consumed on every supported platform, so validate against
// the portable subset rather than the current host filesystem's rules.
bool IsSafeUpdaterRelativePath(std::string_view path, bool allow_trailing_separator = false);
std::string CanonicalUpdaterRelativePath(std::string_view path);
// Produces a host-independent key for rejecting separator and ASCII case
// aliases before any archive or manifest path reaches the filesystem.
std::string UpdaterPathCollisionKey(std::string_view path);
// Resolves a validated relative path beneath a canonical updater-owned root
// while rejecting every pre-existing redirecting component. Missing suffixes
// are returned lexically so callers can create new files/directories, then
// re-run the check immediately before I/O.
std::filesystem::path ResolveUpdaterPathWithoutRedirects(
    const std::filesystem::path &canonical_root, std::string_view relative_path);

} // namespace worr::updater
