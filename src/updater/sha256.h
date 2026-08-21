#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace worr::updater {

class Sha256 final {
public:
  Sha256();

  void Update(const uint8_t *bytes, size_t size);
  std::array<uint8_t, 32> Final();

private:
  static uint32_t RotateRight(uint32_t value, uint32_t bits);
  void Reset();
  void Transform();

  std::array<uint8_t, 64> data_{};
  uint32_t data_length_ = 0;
  uint64_t bit_length_ = 0;
  std::array<uint32_t, 8> state_{};
};

std::array<uint8_t, 32> Sha256Digest(const uint8_t *bytes, size_t size);
std::string Sha256Hex(const uint8_t *bytes, size_t size);
std::string Sha256File(const std::filesystem::path &path);

} // namespace worr::updater
