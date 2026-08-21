#include "archive_path.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace worr::updater {

namespace {

constexpr size_t kMaxUpdaterPathBytes = 4096;
constexpr size_t kMaxUpdaterComponentBytes = 255;

bool IsValidUtf8(std::string_view value) {
  for (size_t index = 0; index < value.size();) {
    const unsigned char lead = static_cast<unsigned char>(value[index]);
    if (lead < 0x80) {
      ++index;
      continue;
    }

    size_t continuation_count = 0;
    uint32_t code_point = 0;
    if ((lead & 0xE0) == 0xC0) {
      continuation_count = 1;
      code_point = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      continuation_count = 2;
      code_point = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      continuation_count = 3;
      code_point = lead & 0x07;
    } else {
      return false;
    }
    if (index + continuation_count >= value.size())
      return false;
    for (size_t offset = 1; offset <= continuation_count; ++offset) {
      const unsigned char continuation = static_cast<unsigned char>(value[index + offset]);
      if ((continuation & 0xC0) != 0x80)
        return false;
      code_point = (code_point << 6) | (continuation & 0x3F);
    }

    const uint32_t minimum = continuation_count == 1 ? 0x80 : continuation_count == 2 ? 0x800 : 0x10000;
    if (code_point < minimum || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF))
      return false;
    index += continuation_count + 1;
  }
  return true;
}

bool IsReservedWindowsName(std::string_view component) {
  const size_t extension = component.find('.');
  std::string stem(component.substr(0, extension));
  std::transform(stem.begin(), stem.end(), stem.begin(),
                 [](unsigned char value) { return static_cast<char>(std::toupper(value)); });

  static constexpr std::array<std::string_view, 7> reserved = {
      "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$",
  };
  if (std::find(reserved.begin(), reserved.end(), stem) != reserved.end())
    return true;

  if (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) {
    const std::string_view number(stem.data() + 3, stem.size() - 3);
    static constexpr std::array<std::string_view, 12> reserved_numbers = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", u8"¹", u8"²", u8"³",
    };
    if (std::find(reserved_numbers.begin(), reserved_numbers.end(), number) != reserved_numbers.end())
      return true;
  }

  return false;
}

bool IsSafeComponent(std::string_view component) {
  if (component.empty() || component == "." || component == "..")
    return false;
  if (component.size() > kMaxUpdaterComponentBytes)
    return false;
  if (component.back() == ' ' || component.back() == '.')
    return false;

  for (unsigned char value : component) {
    if (value < 32 || value >= 128 || value == '<' || value == '>' || value == ':' || value == '"' || value == '|' ||
        value == '?' || value == '*')
      return false;
  }

  return !IsReservedWindowsName(component);
}

uintmax_t GetUpdaterFileLinkCount(const std::filesystem::path &path) {
#if defined(_WIN32)
  const HANDLE handle = CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    throw std::filesystem::filesystem_error(
        "Could not open updater-owned file for inspection", path,
        std::error_code(static_cast<int>(error), std::system_category()));
  }

  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(handle, &information)) {
    const DWORD error = GetLastError();
    CloseHandle(handle);
    throw std::filesystem::filesystem_error(
        "Could not inspect updater-owned file", path,
        std::error_code(static_cast<int>(error), std::system_category()));
  }
  CloseHandle(handle);

  if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    throw std::runtime_error("Updater-owned file became a reparse point: " + path.string());
  return information.nNumberOfLinks;
#else
  std::error_code error;
  const uintmax_t link_count = std::filesystem::hard_link_count(path, error);
  if (error)
    throw std::filesystem::filesystem_error("Could not inspect updater-owned file link count", path, error);
  return link_count;
#endif
}

} // namespace

bool IsSafeUpdaterRelativePath(std::string_view path, bool allow_trailing_separator) {
  if (path.empty() || path.size() > kMaxUpdaterPathBytes || !IsValidUtf8(path))
    return false;

  size_t component_start = 0;
  for (size_t index = 0; index < path.size(); ++index) {
    if (path[index] != '/' && path[index] != '\\')
      continue;

    const std::string_view component = path.substr(component_start, index - component_start);
    if (!IsSafeComponent(component))
      return false;
    if (index + 1 == path.size())
      return allow_trailing_separator;
    component_start = index + 1;
  }

  return IsSafeComponent(path.substr(component_start));
}

std::string CanonicalUpdaterRelativePath(std::string_view path) {
  std::string normalized(path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  return normalized;
}

std::string UpdaterPathCollisionKey(std::string_view path) {
  std::string key = CanonicalUpdaterRelativePath(path);
  if (!key.empty() && key.back() == '/')
    key.pop_back();
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
    if (value >= 'A' && value <= 'Z')
      return static_cast<char>(value + ('a' - 'A'));
    return static_cast<char>(value);
  });
  return key;
}

std::filesystem::path ResolveUpdaterPathWithoutRedirects(
    const std::filesystem::path &canonical_root, std::string_view relative_path) {
  if (!IsSafeUpdaterRelativePath(relative_path))
    throw std::runtime_error("Unsafe updater-owned path: " + std::string(relative_path));
  if (!std::filesystem::is_directory(canonical_root))
    throw std::runtime_error("Updater-owned root is not a directory: " + canonical_root.string());

  const std::filesystem::path relative =
      std::filesystem::u8path(CanonicalUpdaterRelativePath(relative_path));
  std::vector<std::filesystem::path> components(relative.begin(), relative.end());
  std::filesystem::path candidate = canonical_root;
  bool missing_parent = false;

  for (size_t index = 0; index < components.size(); ++index) {
    candidate /= components[index];
    if (missing_parent)
      continue;

    bool exists = false;
    bool is_directory = false;
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(candidate.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = GetLastError();
      if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        throw std::filesystem::filesystem_error(
            "Could not inspect updater-owned path", candidate,
            std::error_code(static_cast<int>(error), std::system_category()));
    } else {
      exists = true;
      if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        throw std::runtime_error("Updater-owned path contains a reparse point: " + candidate.string());
      is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
#else
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(candidate, error);
    if (error) {
      if (error != std::errc::no_such_file_or_directory)
        throw std::filesystem::filesystem_error("Could not inspect updater-owned path", candidate, error);
    } else if (status.type() != std::filesystem::file_type::not_found) {
      exists = true;
      if (std::filesystem::is_symlink(status))
        throw std::runtime_error("Updater-owned path contains a symbolic link: " + candidate.string());
      is_directory = std::filesystem::is_directory(status);
    }
#endif

    if (!exists) {
      missing_parent = true;
      continue;
    }
    if (index + 1 < components.size() && !is_directory)
      throw std::runtime_error("Updater-owned path has a non-directory component: " + candidate.string());
    if (index + 1 == components.size() && !is_directory && GetUpdaterFileLinkCount(candidate) > 1)
      throw std::runtime_error("Updater-owned file has hard-link aliases: " + candidate.string());
  }

  return candidate;
}

} // namespace worr::updater
