#include "release_signature.h"

#include "sha256.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <monocypher-ed25519.h>

namespace worr::updater {

namespace {

constexpr int kSignatureSchemaVersion = 1;
constexpr std::string_view kSignatureAlgorithm = "ed25519";
constexpr std::string_view kReleaseIndexMediaType = "application/vnd.worr.release-index.v3+json";
constexpr char kSigningDomainBytes[] = "WORR release index signature v1\0";
constexpr std::string_view kSigningDomain(kSigningDomainBytes, sizeof(kSigningDomainBytes) - 1);
constexpr size_t kMaxReleaseIndexSize = 32u * 1024u * 1024u;
constexpr size_t kMaxSignatureEnvelopeSize = 1024u * 1024u;
constexpr size_t kMaxPolicyKeys = 32;
constexpr size_t kMaxSignatures = 64;
constexpr size_t kMaxScopesPerKey = 64;
constexpr size_t kMaxScopeBytes = 255;
constexpr size_t kMaxPayloadNameBytes = 255;

[[noreturn]] void Invalid(std::string message) {
  throw std::runtime_error("Invalid release signature: " + std::move(message));
}

Json::Value ParseStrictJson(std::string_view text, std::string_view description) {
  Json::CharReaderBuilder builder;
  builder["allowComments"] = false;
  builder["allowTrailingCommas"] = false;
  builder["collectComments"] = false;
  builder["failIfExtra"] = true;
  builder["rejectDupKeys"] = true;
  builder["strictRoot"] = true;

  Json::Value root;
  std::string errors;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors))
    Invalid(std::string(description) + " is not strict JSON (" + errors + ")");
  return root;
}

void RequireExactFields(const Json::Value &value, std::initializer_list<std::string_view> fields,
                        std::string_view description) {
  if (!value.isObject())
    Invalid(std::string(description) + " must be an object");

  std::set<std::string> expected;
  for (std::string_view field : fields)
    expected.emplace(field);
  std::set<std::string> actual;
  for (const std::string &name : value.getMemberNames())
    actual.insert(name);
  if (actual != expected)
    Invalid(std::string(description) + " has unexpected or missing fields");
}

bool IsLowerHex(std::string_view value, size_t length) {
  return value.size() == length &&
         std::all_of(value.begin(), value.end(), [](unsigned char byte) {
           return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
         });
}

int Base64UrlDigit(unsigned char byte) {
  if (byte >= 'A' && byte <= 'Z')
    return byte - 'A';
  if (byte >= 'a' && byte <= 'z')
    return byte - 'a' + 26;
  if (byte >= '0' && byte <= '9')
    return byte - '0' + 52;
  if (byte == '-')
    return 62;
  if (byte == '_')
    return 63;
  return -1;
}

template <size_t Size>
std::string Base64UrlEncode(const std::array<uint8_t, Size> &bytes) {
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string encoded;
  encoded.reserve((bytes.size() * 4 + 2) / 3);
  uint32_t accumulator = 0;
  unsigned int bits = 0;
  for (uint8_t byte : bytes) {
    accumulator = (accumulator << 8u) | byte;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      encoded.push_back(alphabet[(accumulator >> bits) & 0x3fu]);
    }
  }
  if (bits != 0)
    encoded.push_back(alphabet[(accumulator << (6u - bits)) & 0x3fu]);
  return encoded;
}

template <size_t Size>
std::array<uint8_t, Size> DecodeBase64Url(const Json::Value &value, std::string_view description) {
  if (!value.isString())
    Invalid(std::string(description) + " must be canonical unpadded base64url");
  const std::string encoded = value.asString();

  std::array<uint8_t, Size> decoded{};
  size_t output = 0;
  uint32_t accumulator = 0;
  unsigned int bits = 0;
  for (unsigned char byte : encoded) {
    const int digit = Base64UrlDigit(byte);
    if (digit < 0)
      Invalid(std::string(description) + " must be canonical unpadded base64url");
    accumulator = (accumulator << 6u) | static_cast<uint32_t>(digit);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (output >= decoded.size())
        Invalid(std::string(description) + " has the wrong decoded size");
      decoded[output++] = static_cast<uint8_t>((accumulator >> bits) & 0xffu);
    }
  }
  if (output != decoded.size() || Base64UrlEncode(decoded) != encoded)
    Invalid(std::string(description) + " has the wrong size or non-canonical encoding");
  return decoded;
}

std::vector<std::string> ParseScopes(const Json::Value &value, std::string_view description) {
  if (!value.isArray() || value.empty() || value.size() > kMaxScopesPerKey)
    Invalid(std::string(description) + " must be a bounded non-empty array");
  std::vector<std::string> scopes;
  std::set<std::string> seen;
  scopes.reserve(value.size());
  for (const Json::Value &item : value) {
    if (!item.isString())
      Invalid(std::string(description) + " entries must be strings");
    std::string scope = item.asString();
    const bool printable_ascii =
        !scope.empty() && scope.size() <= kMaxScopeBytes &&
        std::all_of(scope.begin(), scope.end(), [](unsigned char byte) { return byte >= 0x20 && byte <= 0x7e; });
    if (!printable_ascii || std::isspace(static_cast<unsigned char>(scope.front())) != 0 ||
        std::isspace(static_cast<unsigned char>(scope.back())) != 0)
      Invalid(std::string(description) + " entries must be trimmed printable ASCII");
    if (!seen.insert(scope).second)
      Invalid(std::string(description) + " contains a duplicate");
    scopes.push_back(std::move(scope));
  }
  return scopes;
}

bool Contains(const std::vector<std::string> &values, std::string_view expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

void AppendBigEndian(std::vector<uint8_t> &output, uint64_t value, size_t bytes) {
  for (size_t index = 0; index < bytes; ++index) {
    const size_t shift = (bytes - index - 1) * 8;
    output.push_back(static_cast<uint8_t>((value >> shift) & 0xffu));
  }
}

std::vector<uint8_t> BuildSigningInput(std::string_view payload, std::string_view payload_name) {
  if (payload.empty() || payload.size() > kMaxReleaseIndexSize)
    Invalid("release index size is outside the supported range");
  if (payload_name.empty() || payload_name.size() > kMaxPayloadNameBytes ||
      payload_name == "." || payload_name == ".." ||
      std::any_of(payload_name.begin(), payload_name.end(), [](unsigned char byte) {
        return byte < 0x20 || byte > 0x7e || byte == '/' || byte == '\\';
      }))
    Invalid("release index name is not a printable ASCII filename");

  std::vector<uint8_t> signing_input;
  signing_input.reserve(kSigningDomain.size() + 2 + payload_name.size() + 2 +
                        kReleaseIndexMediaType.size() + 8 + payload.size());
  signing_input.insert(signing_input.end(), kSigningDomain.begin(), kSigningDomain.end());
  AppendBigEndian(signing_input, payload_name.size(), 2);
  signing_input.insert(signing_input.end(), payload_name.begin(), payload_name.end());
  AppendBigEndian(signing_input, kReleaseIndexMediaType.size(), 2);
  signing_input.insert(signing_input.end(), kReleaseIndexMediaType.begin(), kReleaseIndexMediaType.end());
  AppendBigEndian(signing_input, payload.size(), 8);
  signing_input.insert(signing_input.end(), payload.begin(), payload.end());
  return signing_input;
}

} // namespace

ReleaseTrustPolicy ParseReleaseTrustPolicy(const Json::Value &value) {
  RequireExactFields(value, {"schema_version", "threshold", "keys"}, "release trust policy");
  if (!value["schema_version"].isInt() || value["schema_version"].asInt() != kSignatureSchemaVersion)
    Invalid("release trust policy has an unsupported schema_version");
  if (!value["threshold"].isUInt64() || value["threshold"].asUInt64() == 0)
    Invalid("release trust policy threshold must be a positive integer");
  const Json::Value &keys = value["keys"];
  if (!keys.isArray() || keys.empty() || keys.size() > kMaxPolicyKeys)
    Invalid("release trust policy keys must be a bounded non-empty array");
  if (value["threshold"].asUInt64() > keys.size())
    Invalid("release trust policy threshold exceeds its key count");

  ReleaseTrustPolicy policy;
  policy.threshold = static_cast<size_t>(value["threshold"].asUInt64());
  std::set<std::string> key_ids;
  std::set<std::array<uint8_t, 32>> public_keys;
  policy.keys.reserve(keys.size());
  for (Json::ArrayIndex index = 0; index < keys.size(); ++index) {
    const Json::Value &item = keys[index];
    RequireExactFields(item, {"key_id", "algorithm", "public_key", "repos", "channels"},
                       "release trust policy key");
    if (!item["algorithm"].isString() || item["algorithm"].asString() != kSignatureAlgorithm)
      Invalid("release trust policy key algorithm must be ed25519");
    if (!item["key_id"].isString() || !IsLowerHex(item["key_id"].asString(), 64))
      Invalid("release trust policy key has an invalid key_id");

    TrustedReleaseKey key;
    key.key_id = item["key_id"].asString();
    key.public_key = DecodeBase64Url<32>(item["public_key"], "release trust policy public key");
    if (Sha256Hex(key.public_key.data(), key.public_key.size()) != key.key_id)
      Invalid("release trust policy key_id does not match its public key");
    key.repos = ParseScopes(item["repos"], "release trust policy repos");
    key.channels = ParseScopes(item["channels"], "release trust policy channels");

    if (!key_ids.insert(key.key_id).second || !public_keys.insert(key.public_key).second)
      Invalid("release trust policy contains a duplicate key");
    policy.keys.push_back(std::move(key));
  }
  return policy;
}

VerifiedReleaseIndex VerifyReleaseIndexSignature(
    std::string_view raw_index, std::string_view index_name,
    std::string_view raw_signature_envelope, const ReleaseTrustPolicy &policy,
    std::string_view expected_repo, std::string_view expected_channel) {
  if (policy.threshold == 0 || policy.threshold > policy.keys.size())
    Invalid("release trust policy is unusable");
  if (raw_signature_envelope.empty() || raw_signature_envelope.size() > kMaxSignatureEnvelopeSize)
    Invalid("signature envelope size is outside the supported range");

  const std::vector<uint8_t> signing_input = BuildSigningInput(raw_index, index_name);
  const Json::Value envelope = ParseStrictJson(raw_signature_envelope, "release signature envelope");
  RequireExactFields(envelope, {"schema_version", "payload", "signatures"}, "signature envelope");
  if (!envelope["schema_version"].isInt() || envelope["schema_version"].asInt() != kSignatureSchemaVersion)
    Invalid("signature envelope has an unsupported schema_version");

  const Json::Value &descriptor = envelope["payload"];
  RequireExactFields(descriptor, {"name", "media_type", "size", "sha256"}, "signature payload descriptor");
  if (!descriptor["name"].isString() || descriptor["name"].asString() != index_name)
    Invalid("signature envelope payload name mismatch");
  if (!descriptor["media_type"].isString() || descriptor["media_type"].asString() != kReleaseIndexMediaType)
    Invalid("signature envelope media type mismatch");
  if (!descriptor["size"].isUInt64() || descriptor["size"].asUInt64() != raw_index.size())
    Invalid("signature envelope payload size mismatch");
  if (!descriptor["sha256"].isString() || !IsLowerHex(descriptor["sha256"].asString(), 64) ||
      descriptor["sha256"].asString() !=
          Sha256Hex(reinterpret_cast<const uint8_t *>(raw_index.data()), raw_index.size()))
    Invalid("signature envelope payload sha256 mismatch");

  Json::Value index = ParseStrictJson(raw_index, "release index");
  if (!index.isObject() || !index["repo"].isString() || !index["channel"].isString())
    Invalid("release index has invalid repo/channel identity");
  const std::string repo = index["repo"].asString();
  const std::string channel = index["channel"].asString();
  if (repo != expected_repo)
    Invalid("release index repository does not match the updater configuration");
  if (channel != expected_channel)
    Invalid("release index channel does not match the updater configuration");

  const Json::Value &signatures = envelope["signatures"];
  if (!signatures.isArray() || signatures.empty() || signatures.size() > kMaxSignatures)
    Invalid("signature envelope signatures must be a bounded non-empty array");

  std::unordered_map<std::string, const TrustedReleaseKey *> trusted;
  for (const TrustedReleaseKey &key : policy.keys)
    trusted.emplace(key.key_id, &key);

  std::set<std::string> seen;
  std::vector<std::string> verified;
  for (Json::ArrayIndex index_value = 0; index_value < signatures.size(); ++index_value) {
    const Json::Value &signature_value = signatures[index_value];
    RequireExactFields(signature_value, {"key_id", "algorithm", "signature"}, "release signature");
    if (!signature_value["key_id"].isString() || !IsLowerHex(signature_value["key_id"].asString(), 64))
      Invalid("release signature has an invalid key_id");
    const std::string key_id = signature_value["key_id"].asString();
    if (!seen.insert(key_id).second)
      Invalid("signature envelope repeats a key_id");
    if (!signature_value["algorithm"].isString() ||
        signature_value["algorithm"].asString() != kSignatureAlgorithm)
      Invalid("release signature has an unsupported algorithm");
    const std::array<uint8_t, 64> signature =
        DecodeBase64Url<64>(signature_value["signature"], "release signature");

    const auto trusted_it = trusted.find(key_id);
    if (trusted_it == trusted.end())
      continue;
    const TrustedReleaseKey &key = *trusted_it->second;
    if (!Contains(key.repos, repo) || !Contains(key.channels, channel))
      continue;
    if (crypto_ed25519_check(signature.data(), key.public_key.data(), signing_input.data(), signing_input.size()) == 0)
      verified.push_back(key_id);
  }

  if (verified.size() < policy.threshold)
    Invalid("release index signature threshold was not met");
  std::sort(verified.begin(), verified.end());
  return {std::move(index), std::move(verified)};
}

} // namespace worr::updater
