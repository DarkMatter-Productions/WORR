#include "sha256.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace worr::updater {

namespace {

constexpr std::array<uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u,
};

std::string HexDigest(const std::array<uint8_t, 32> &digest) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (uint8_t byte : digest)
    stream << std::setw(2) << static_cast<unsigned int>(byte);
  return stream.str();
}

} // namespace

Sha256::Sha256() { Reset(); }

void Sha256::Reset() {
  data_length_ = 0;
  bit_length_ = 0;
  state_[0] = 0x6a09e667u;
  state_[1] = 0xbb67ae85u;
  state_[2] = 0x3c6ef372u;
  state_[3] = 0xa54ff53au;
  state_[4] = 0x510e527fu;
  state_[5] = 0x9b05688cu;
  state_[6] = 0x1f83d9abu;
  state_[7] = 0x5be0cd19u;
}

void Sha256::Update(const uint8_t *bytes, size_t size) {
  if (size != 0 && bytes == nullptr)
    throw std::invalid_argument("SHA-256 input is null");
  for (size_t offset = 0; offset < size; ++offset) {
    const uint8_t byte = bytes[offset];
    data_[data_length_++] = byte;
    if (data_length_ == data_.size()) {
      Transform();
      bit_length_ += 512;
      data_length_ = 0;
    }
  }
}

std::array<uint8_t, 32> Sha256::Final() {
  uint32_t index = data_length_;
  if (data_length_ < 56) {
    data_[index++] = 0x80;
    while (index < 56)
      data_[index++] = 0;
  } else {
    data_[index++] = 0x80;
    while (index < 64)
      data_[index++] = 0;
    Transform();
    std::fill(data_.begin(), data_.begin() + 56, 0);
  }

  bit_length_ += static_cast<uint64_t>(data_length_) * 8u;
  for (uint32_t offset = 0; offset < 8; ++offset)
    data_[63 - offset] = static_cast<uint8_t>(bit_length_ >> (offset * 8u));
  Transform();

  std::array<uint8_t, 32> hash{};
  for (uint32_t word = 0; word < state_.size(); ++word) {
    for (uint32_t byte = 0; byte < 4; ++byte)
      hash[word * 4 + byte] = static_cast<uint8_t>((state_[word] >> (24u - byte * 8u)) & 0xffu);
  }
  return hash;
}

uint32_t Sha256::RotateRight(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32u - bits));
}

void Sha256::Transform() {
  std::array<uint32_t, 64> words{};
  for (uint32_t index = 0, input = 0; index < 16; ++index, input += 4) {
    words[index] = (static_cast<uint32_t>(data_[input]) << 24u) |
                   (static_cast<uint32_t>(data_[input + 1]) << 16u) |
                   (static_cast<uint32_t>(data_[input + 2]) << 8u) |
                   static_cast<uint32_t>(data_[input + 3]);
  }
  for (uint32_t index = 16; index < words.size(); ++index) {
    const uint32_t s0 = RotateRight(words[index - 15], 7u) ^ RotateRight(words[index - 15], 18u) ^
                        (words[index - 15] >> 3u);
    const uint32_t s1 = RotateRight(words[index - 2], 17u) ^ RotateRight(words[index - 2], 19u) ^
                        (words[index - 2] >> 10u);
    words[index] = words[index - 16] + s0 + words[index - 7] + s1;
  }

  uint32_t a = state_[0];
  uint32_t b = state_[1];
  uint32_t c = state_[2];
  uint32_t d = state_[3];
  uint32_t e = state_[4];
  uint32_t f = state_[5];
  uint32_t g = state_[6];
  uint32_t h = state_[7];

  for (uint32_t index = 0; index < words.size(); ++index) {
    const uint32_t s1 = RotateRight(e, 6u) ^ RotateRight(e, 11u) ^ RotateRight(e, 25u);
    const uint32_t choose = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + s1 + choose + kSha256RoundConstants[index] + words[index];
    const uint32_t s0 = RotateRight(a, 2u) ^ RotateRight(a, 13u) ^ RotateRight(a, 22u);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + majority;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

std::array<uint8_t, 32> Sha256Digest(const uint8_t *bytes, size_t size) {
  Sha256 hasher;
  hasher.Update(bytes, size);
  return hasher.Final();
}

std::string Sha256Hex(const uint8_t *bytes, size_t size) {
  return HexDigest(Sha256Digest(bytes, size));
}

std::string Sha256File(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("Could not open " + path.generic_string());

  Sha256 hasher;
  std::array<uint8_t, 1 << 15> buffer{};
  while (file) {
    file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = file.gcount();
    if (count > 0)
      hasher.Update(buffer.data(), static_cast<size_t>(count));
  }
  if (file.bad())
    throw std::runtime_error("Could not read " + path.generic_string());
  return HexDigest(hasher.Final());
}

} // namespace worr::updater
