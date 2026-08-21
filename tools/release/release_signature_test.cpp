#include "release_signature.h"
#include "sha256.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <json/json.h>

namespace {

using worr::updater::ParseReleaseTrustPolicy;
using worr::updater::ReleaseTrustPolicy;
using worr::updater::Sha256File;
using worr::updater::Sha256Hex;
using worr::updater::VerifyReleaseIndexSignature;

constexpr std::string_view kPayload = R"({
  "schema_version": 3,
  "repo": "example/WORR",
  "channel": "stable",
  "version": "1.2.3",
  "targets": []
}
)";

constexpr std::string_view kEnvelope = R"({
  "payload": {
    "media_type": "application/vnd.worr.release-index.v3+json",
    "name": "index.json",
    "sha256": "06150af8e91ad01431513d35dee814d1a73c80c9ebfc4a185157cb2b847528de",
    "size": 114
  },
  "schema_version": 1,
  "signatures": [
    {
      "algorithm": "ed25519",
      "key_id": "56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c",
      "signature": "HQWzKNJmqcEkjix_ULhNgoEsYg58Ox3p4nDTYcFc0c6XOgDqIXCdEAdJ4mcMohKe1clkPXrGiZrG7e-lPOkjBA"
    }
  ]
}
)";

constexpr std::string_view kPolicy = R"({
  "schema_version": 1,
  "threshold": 1,
  "keys": [
    {
      "key_id": "56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c",
      "algorithm": "ed25519",
      "public_key": "A6EHv_POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg",
      "repos": ["example/WORR"],
      "channels": ["stable"]
    }
  ]
}
)";

constexpr std::string_view kThresholdPayload =
    "{\"schema_version\":3,\"repo\":\"example/WORR\",\"channel\":"
    "\"stable\",\"version\":\"2.0.0\",\"targets\":[]}\n";

constexpr std::string_view kThresholdEnvelope = R"({
  "payload": {
    "media_type": "application/vnd.worr.release-index.v3+json",
    "name": "threshold.json",
    "sha256": "372228174a2fd6a968ac394c05fa6aeae4d52d3d3c315bc1101e2ab8ae7f3d0d",
    "size": 93
  },
  "schema_version": 1,
  "signatures": [
    {
      "algorithm": "ed25519",
      "key_id": "24f6ed6acbfe1009c030d7ca567c33ca4830911498236b5561a6c82abec5de28",
      "signature": "sqQwhy3WitP8tU5gyMuQ4RIITApQ7Nvugsx2mrW_CsnU1EywHKZQtW_EitN-hpMQk0aILcBUjgA9brs70mkVCA"
    },
    {
      "algorithm": "ed25519",
      "key_id": "56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c",
      "signature": "cskLbg6kRuva5mXc-sMy9mfbSHPzFDfhd4ACLO_xXQD2sM6IeHSRekjNDu-iI0cWzjtKnMOMywT3Cm2UtVZMCQ"
    }
  ]
}
)";

constexpr std::string_view kThresholdPolicy = R"({
  "schema_version": 1,
  "threshold": 2,
  "keys": [
    {
      "key_id": "56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c",
      "algorithm": "ed25519",
      "public_key": "A6EHv_POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg",
      "repos": ["example/WORR"],
      "channels": ["stable"]
    },
    {
      "key_id": "24f6ed6acbfe1009c030d7ca567c33ca4830911498236b5561a6c82abec5de28",
      "algorithm": "ed25519",
      "public_key": "Kay64UG8yvCyLhqU000LxzYeUm0L_hLIl5S8kyKWbdc",
      "repos": ["example/WORR"],
      "channels": ["stable"]
    }
  ]
}
)";

Json::Value ParseJson(std::string_view text) {
  Json::CharReaderBuilder builder;
  builder["allowComments"] = false;
  builder["allowTrailingCommas"] = false;
  builder["failIfExtra"] = true;
  builder["rejectDupKeys"] = true;
  builder["strictRoot"] = true;
  Json::Value value;
  std::string errors;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  if (!reader->parse(text.data(), text.data() + text.size(), &value, &errors))
    throw std::runtime_error("Test JSON is invalid: " + errors);
  return value;
}

void ExpectRejected(const std::function<void()> &operation,
                    std::string_view label) {
  try {
    operation();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error(std::string(label) + " was accepted");
}

std::string ReplaceOnce(std::string text, std::string_view before,
                        std::string_view after) {
  const size_t position = text.find(before);
  if (position == std::string::npos)
    throw std::runtime_error("Test fixture replacement was not found");
  text.replace(position, before.size(), after);
  return text;
}

} // namespace

int main() {
  try {
    const std::string abc = "abc";
    const auto *abc_bytes = reinterpret_cast<const uint8_t *>(abc.data());
    if (Sha256Hex(abc_bytes, abc.size()) !=
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
      throw std::runtime_error("SHA-256 known-answer test failed");

    const std::array<std::pair<size_t, std::string_view>, 6> boundary_vectors =
        {{
            {0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b8"
                "55"},
            {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734"
                 "318"},
            {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec67"
                 "38a"},
            {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2"
                 "f34"},
            {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df15466"
                 "8eb"},
            {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0"
                 "ae0"},
        }};
    for (const auto &[size, expected] : boundary_vectors) {
      const std::string input(size, 'a');
      if (Sha256Hex(reinterpret_cast<const uint8_t *>(input.data()),
                    input.size()) != expected)
        throw std::runtime_error("SHA-256 boundary known-answer test failed");
    }

    const std::filesystem::path hash_file =
        std::filesystem::temp_directory_path() /
        "worr-release-signature-sha256-file-vector.bin";
    {
      std::ofstream output(hash_file, std::ios::binary | std::ios::trunc);
      output << std::string(1000, 'a');
    }
    const std::string file_hash = Sha256File(hash_file);
    std::error_code remove_error;
    std::filesystem::remove(hash_file, remove_error);
    if (file_hash !=
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3")
      throw std::runtime_error("SHA-256 file known-answer test failed");

    const ReleaseTrustPolicy policy =
        ParseReleaseTrustPolicy(ParseJson(kPolicy));
    const auto verified = VerifyReleaseIndexSignature(
        kPayload, "index.json", kEnvelope, policy, "example/WORR", "stable");
    if (verified.index["version"].asString() != "1.2.3" ||
        verified.verified_key_ids.size() != 1)
      throw std::runtime_error(
          "Valid cross-implementation release signature did not verify");

    const ReleaseTrustPolicy threshold_policy =
        ParseReleaseTrustPolicy(ParseJson(kThresholdPolicy));
    const auto threshold_verified = VerifyReleaseIndexSignature(
        kThresholdPayload, "threshold.json", kThresholdEnvelope,
        threshold_policy, "example/WORR", "stable");
    if (threshold_verified.verified_key_ids.size() != 2)
      throw std::runtime_error(
          "Valid cross-language threshold-2 signature did not verify");

    Json::StreamWriterBuilder compact_writer;
    compact_writer["indentation"] = "";
    Json::Value insufficient_envelope = ParseJson(kThresholdEnvelope);
    insufficient_envelope["signatures"].resize(1);
    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(
              kThresholdPayload, "threshold.json",
              Json::writeString(compact_writer, insufficient_envelope),
              threshold_policy, "example/WORR", "stable");
        },
        "signature envelope below threshold");

    Json::Value duplicate_signature = ParseJson(kThresholdEnvelope);
    duplicate_signature["signatures"].append(
        duplicate_signature["signatures"][0]);
    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(
              kThresholdPayload, "threshold.json",
              Json::writeString(compact_writer, duplicate_signature),
              threshold_policy, "example/WORR", "stable");
        },
        "duplicate threshold signature");

    const std::string malformed_base64 =
        ReplaceOnce(std::string(kThresholdEnvelope), "sqQwhy", "+qQwhy");
    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(kThresholdPayload, "threshold.json",
                                      malformed_base64, threshold_policy,
                                      "example/WORR", "stable");
        },
        "malformed signature base64url");

    const std::string tampered_payload =
        ReplaceOnce(std::string(kPayload), "1.2.3", "9.9.9");
    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(tampered_payload, "index.json", kEnvelope,
                                      policy, "example/WORR", "stable");
        },
        "tampered release index");

    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(kPayload, "renamed.json", kEnvelope,
                                      policy, "example/WORR", "stable");
        },
        "renamed release index");
    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(kPayload, std::string(256, 'a'),
                                      kEnvelope, policy, "example/WORR",
                                      "stable");
        },
        "oversized release index name");

    const std::string tampered_signature =
        ReplaceOnce(std::string(kEnvelope), "HQWz", "IQWz");
    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(kPayload, "index.json",
                                      tampered_signature, policy,
                                      "example/WORR", "stable");
        },
        "tampered signature");

    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(kPayload, "index.json", kEnvelope, policy,
                                      "different/WORR", "stable");
        },
        "wrong repository scope");
    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(kPayload, "index.json", kEnvelope, policy,
                                      "example/WORR", "nightly");
        },
        "wrong channel scope");

    const std::string duplicate_schema = ReplaceOnce(
        std::string(kEnvelope), "{\n", "{\n  \"schema_version\": 1,\n");
    ExpectRejected(
        [&] {
          VerifyReleaseIndexSignature(kPayload, "index.json", duplicate_schema,
                                      policy, "example/WORR", "stable");
        },
        "duplicate envelope field");

    Json::Value mismatched_policy = ParseJson(kPolicy);
    mismatched_policy["keys"][0]["key_id"] = std::string(64, '0');
    ExpectRejected([&] { ParseReleaseTrustPolicy(mismatched_policy); },
                   "mismatched policy key identity");

    Json::Value private_policy = ParseJson(kPolicy);
    private_policy["keys"][0]["private_key"] = "must-never-be-accepted";
    ExpectRejected([&] { ParseReleaseTrustPolicy(private_policy); },
                   "private key material in trust policy");
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
