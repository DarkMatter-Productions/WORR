#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace worr::updater {

void ValidateUpdaterZipEntryAttributes(std::string_view path, bool is_directory,
                                       uint16_t version_made_by,
                                       uint32_t external_attributes);
uint32_t UpdaterZipEntryMode(bool is_directory, uint16_t version_made_by,
                             uint32_t external_attributes);
void ApplyUpdaterZipEntryAttributes(const std::filesystem::path &path, bool is_directory,
                                    uint16_t version_made_by, uint32_t external_attributes);

} // namespace worr::updater
