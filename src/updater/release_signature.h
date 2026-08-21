#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <json/json.h>

namespace worr::updater {

struct TrustedReleaseKey {
  std::string key_id;
  std::array<uint8_t, 32> public_key{};
  std::vector<std::string> repos;
  std::vector<std::string> channels;
};

struct ReleaseTrustPolicy {
  size_t threshold = 0;
  std::vector<TrustedReleaseKey> keys;
};

struct VerifiedReleaseIndex {
  Json::Value index;
  std::vector<std::string> verified_key_ids;
};

ReleaseTrustPolicy ParseReleaseTrustPolicy(const Json::Value &value);

// Verifies the detached signature envelope over the exact release-index bytes
// before returning parsed index data. Throws on any malformed or unauthorized
// input; callers must not parse or act on the index before this succeeds.
VerifiedReleaseIndex VerifyReleaseIndexSignature(
    std::string_view raw_index, std::string_view index_name,
    std::string_view raw_signature_envelope, const ReleaseTrustPolicy &policy,
    std::string_view expected_repo, std::string_view expected_channel);

} // namespace worr::updater
