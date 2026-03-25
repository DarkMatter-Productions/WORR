#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "bootstrap_shared.h"
#include "common/bootstrap.h"

#include <SDL3/SDL.h>
#include <curl/curl.h>
#include <json/json.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "miniz.h"
#include "bootstrap_logo_rgba.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <shellapi.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

namespace worr::updater {

namespace fs = std::filesystem;
using clock_type = std::chrono::steady_clock;

namespace {

constexpr const char *kConfigName = "worr_update.json";
constexpr const char *kStateName = "worr_update_state.json";
constexpr const char *kLocalManifestName = "worr_install_manifest.json";
constexpr const char *kUpdaterStem =
#if defined(_WIN32)
    "worr_updater_" CPUSTRING ".exe";
#else
    "worr_updater_" CPUSTRING;
#endif
constexpr const char *kClientLaunchStem =
#if defined(_WIN32)
    "worr_" CPUSTRING ".exe";
#else
    "worr_" CPUSTRING;
#endif
constexpr const char *kServerLaunchStem =
#if defined(_WIN32)
    "worr_ded_" CPUSTRING ".exe";
#else
    "worr_ded_" CPUSTRING;
#endif
constexpr const char *kClientEngineLibraryStem =
#if defined(_WIN32)
    "worr_engine_" CPUSTRING ".dll";
#elif defined(__APPLE__)
    "worr_engine_" CPUSTRING ".dylib";
#else
    "worr_engine_" CPUSTRING ".so";
#endif
constexpr const char *kServerEngineLibraryStem =
#if defined(_WIN32)
    "worr_ded_engine_" CPUSTRING ".dll";
#elif defined(__APPLE__)
    "worr_ded_engine_" CPUSTRING ".dylib";
#else
    "worr_ded_engine_" CPUSTRING ".so";
#endif
constexpr const char *kBaseDirEnv = "WORR_BOOTSTRAP_BASEDIR";
constexpr const char *kEngineEntryPoint = "WORR_EngineMain";
constexpr const char *kReadyCallbackEntryPoint = "Com_SetBootstrapReadyCallback";
constexpr const char *kSkipUpdateCheckArg = "--bootstrap-skip-update-check";
constexpr const char *kQuietStatusArg = "--bootstrap-quiet-status";
constexpr int kConnectTimeoutMs = 2000;
constexpr int kDiscoveryBudgetMs = 5000;
constexpr int kApplyRetryCount = 20;
constexpr int kApplyRetryDelayMs = 100;
constexpr uint64_t kUiTickDelayMs = 16;
constexpr const char *kUserAgent = "WORR-Bootstrap/1.0";

struct SemverIdentifier {
  bool numeric = false;
  uint64_t number = 0;
  std::string text;
};

struct SemverVersion {
  bool valid = false;
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::vector<SemverIdentifier> prerelease;
};

struct FileEntry {
  std::string path;
  std::string sha256;
  uint64_t size = 0;
};

struct UpdaterConfig {
  std::string repo = "themuffinator/WORR";
  std::string channel = "stable";
  std::string role;
  std::string release_index_asset = "worr-release-index-stable.json";
  std::string launch_exe;
  bool autolaunch = true;
  bool allow_prerelease = false;
  std::vector<std::string> preserve = {
      "worr_update.json",
      "worr_update_state.json",
      "basew/*.cfg",
      "basew/autoexec.cfg",
      "basew/config.cfg",
      "basew/saves/*",
      "basew/screenshots/*",
      "basew/demos/*",
      "basew/logs/*",
  };
};

struct InstallManifest {
  std::string version;
  std::string role;
  std::string launch_exe;
  std::string engine_library;
  std::string local_manifest_name;
  std::vector<FileEntry> files;
};

struct ReleaseAsset {
  std::string name;
  std::string url;
};

struct RemotePayload {
  std::string version;
  std::string tag;
  std::string role;
  std::string launch_exe;
  std::string engine_library;
  std::string update_manifest_name;
  std::string update_package_name;
  std::string local_manifest_name;
  std::string manifest_url;
  std::string package_url;
  Json::Value manifest_json;
};

struct BootstrapOptions {
  Role role = Role::Client;
  fs::path install_root;
  fs::path worker_exe_path;
  std::string launch_relpath;
  std::string engine_library_relpath;
  std::vector<std::string> forwarded_args;
  bool approved_install = false;
  bool worker_mode = false;
  bool skip_update_check = false;
  bool quiet_status = false;
};

using engine_main_fn = int (*)(int argc, char **argv);
using set_ready_callback_fn = void (*)(com_bootstrap_ready_callback_t callback, void *userdata);

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::string Trim(std::string text) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  auto begin = std::find_if(text.begin(), text.end(), not_space);
  auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();
  if (begin >= end)
    return {};
  return std::string(begin, end);
}

std::string NowUtcString() {
  std::time_t now = std::time(nullptr);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  std::ostringstream ss;
  ss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

bool BootstrapTraceEnabled() {
  static int enabled = -1;
  if (enabled == -1) {
    const char *value = std::getenv("WORR_BOOTSTRAP_TRACE");
    enabled = (value && *value && std::string(value) != "0") ? 1 : 0;
  }
  return enabled != 0;
}

void BootstrapTrace(const std::string &message) {
  if (!BootstrapTraceEnabled())
    return;

  try {
    const fs::path path = fs::temp_directory_path() / "worr-bootstrap-trace.log";
    std::ofstream file(path, std::ios::app);
    if (!file.is_open())
      return;
    file << NowUtcString() << " " << message << '\n';
  } catch (...) {
  }
}

std::string RandomToken() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream ss;
  ss << std::hex << dist(gen) << dist(gen);
  return ss.str();
}

fs::path Utf8Path(const std::string &path) { return fs::u8path(path); }

std::string GenericPath(const fs::path &path) { return path.generic_u8string(); }

std::string FoldPathCase(std::string text) {
#if defined(_WIN32)
  return ToLower(std::move(text));
#else
  return text;
#endif
}

std::string NormalizeRelativePath(const fs::path &path) {
  return FoldPathCase(path.lexically_normal().generic_u8string());
}

fs::path NormalizeInstallRoot(fs::path path) {
  std::string text = path.lexically_normal().generic_u8string();
  if (text.size() > 2 && text.compare(text.size() - 2, 2, "/.") == 0)
    text.resize(text.size() - 2);
  while (text.size() > 1 && (text.back() == '/' || text.back() == '\\')) {
#if defined(_WIN32)
    if (text.size() == 3 && std::isalpha(static_cast<unsigned char>(text[0])) && text[1] == ':')
      break;
#endif
    text.pop_back();
  }
  return Utf8Path(text);
}

std::string DefaultLaunchRelpath(Role role) {
  return role == Role::Client ? kClientLaunchStem : kServerLaunchStem;
}

std::string DefaultEngineLibraryRelpath(Role role) {
  return role == Role::Client ? kClientEngineLibraryStem : kServerEngineLibraryStem;
}

bool WildcardMatchRecursive(std::string_view pattern, std::string_view value) {
  while (!pattern.empty()) {
    const char token = pattern.front();
    pattern.remove_prefix(1);
    if (token == '*') {
      if (pattern.empty())
        return true;
      for (size_t i = 0; i <= value.size(); ++i) {
        if (WildcardMatchRecursive(pattern, value.substr(i)))
          return true;
      }
      return false;
    }
    if (value.empty())
      return false;
    const char lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(token)));
    const char rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(value.front())));
    if (token != '?' && lhs != rhs)
      return false;
    value.remove_prefix(1);
  }
  return value.empty();
}

bool MatchesPattern(const std::string &pattern, const std::string &value) {
  return WildcardMatchRecursive(FoldPathCase(pattern), FoldPathCase(value));
}

bool ParseUnsigned(std::string_view text, uint64_t *out) {
  if (text.empty())
    return false;
  uint64_t value = 0;
  for (char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c)))
      return false;
    value = value * 10 + static_cast<uint64_t>(c - '0');
  }
  *out = value;
  return true;
}

bool ParseSemver(const std::string &raw, SemverVersion *out) {
  *out = {};
  std::string value = Trim(raw);
  if (value.empty())
    return false;
  if (value[0] == 'v' || value[0] == 'V')
    value.erase(value.begin());

  const size_t plus = value.find('+');
  if (plus != std::string::npos)
    value.resize(plus);

  std::string core = value;
  std::string prerelease_text;
  const size_t dash = value.find('-');
  if (dash != std::string::npos) {
    core = value.substr(0, dash);
    prerelease_text = value.substr(dash + 1);
  }

  std::array<std::string, 3> parts{};
  {
    std::stringstream ss(core);
    for (int i = 0; i < 3; ++i) {
      if (!std::getline(ss, parts[i], '.'))
        return false;
    }
    if (ss.rdbuf()->in_avail() != 0)
      return false;
  }

  uint64_t major = 0;
  uint64_t minor = 0;
  uint64_t patch = 0;
  if (!ParseUnsigned(parts[0], &major) || !ParseUnsigned(parts[1], &minor) || !ParseUnsigned(parts[2], &patch))
    return false;

  out->valid = true;
  out->major = static_cast<int>(major);
  out->minor = static_cast<int>(minor);
  out->patch = static_cast<int>(patch);

  if (!prerelease_text.empty()) {
    std::stringstream ss(prerelease_text);
    std::string identifier;
    while (std::getline(ss, identifier, '.')) {
      if (identifier.empty())
        return false;
      uint64_t numeric = 0;
      SemverIdentifier item;
      if (ParseUnsigned(identifier, &numeric)) {
        item.numeric = true;
        item.number = numeric;
      } else {
        item.text = identifier;
      }
      out->prerelease.push_back(item);
    }
  }

  return true;
}

int CompareSemver(const std::string &a, const std::string &b) {
  SemverVersion lhs;
  SemverVersion rhs;
  const bool lhs_ok = ParseSemver(a, &lhs);
  const bool rhs_ok = ParseSemver(b, &rhs);
  if (!lhs_ok && !rhs_ok)
    return 0;
  if (!lhs_ok)
    return -1;
  if (!rhs_ok)
    return 1;

  if (lhs.major != rhs.major)
    return lhs.major < rhs.major ? -1 : 1;
  if (lhs.minor != rhs.minor)
    return lhs.minor < rhs.minor ? -1 : 1;
  if (lhs.patch != rhs.patch)
    return lhs.patch < rhs.patch ? -1 : 1;

  if (lhs.prerelease.empty() && rhs.prerelease.empty())
    return 0;
  if (lhs.prerelease.empty())
    return 1;
  if (rhs.prerelease.empty())
    return -1;

  const size_t count = std::min(lhs.prerelease.size(), rhs.prerelease.size());
  for (size_t i = 0; i < count; ++i) {
    const SemverIdentifier &left = lhs.prerelease[i];
    const SemverIdentifier &right = rhs.prerelease[i];
    if (left.numeric && right.numeric) {
      if (left.number != right.number)
        return left.number < right.number ? -1 : 1;
      continue;
    }
    if (left.numeric != right.numeric)
      return left.numeric ? -1 : 1;
    if (left.text != right.text)
      return left.text < right.text ? -1 : 1;
  }
  if (lhs.prerelease.size() == rhs.prerelease.size())
    return 0;
  return lhs.prerelease.size() < rhs.prerelease.size() ? -1 : 1;
}

bool IsPrereleaseVersion(const std::string &version) {
  SemverVersion parsed;
  return ParseSemver(version, &parsed) && !parsed.prerelease.empty();
}

std::optional<Role> ParseRole(const std::string &text) {
  const std::string lowered = ToLower(text);
  if (lowered == "client")
    return Role::Client;
  if (lowered == "server")
    return Role::Server;
  return std::nullopt;
}

bool JsonLoadFile(const fs::path &path, Json::Value *root, std::string *error) {
  std::ifstream file(path);
  if (!file.is_open()) {
    if (error)
      *error = "Could not open " + GenericPath(path);
    return false;
  }

  Json::CharReaderBuilder builder;
  std::string errors;
  if (!Json::parseFromStream(builder, file, root, &errors)) {
    if (error)
      *error = errors;
    return false;
  }
  return true;
}

bool JsonWriteFile(const fs::path &path, const Json::Value &root, std::string *error) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    if (error)
      *error = "Could not write " + GenericPath(path);
    return false;
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  file << Json::writeString(builder, root);
  if (!file.good()) {
    if (error)
      *error = "Failed writing " + GenericPath(path);
    return false;
  }
  return true;
}

std::string JsonString(const Json::Value &root, const char *key, const std::string &fallback = {}) {
  if (root.isMember(key) && root[key].isString())
    return root[key].asString();
  return fallback;
}

std::string JsonStringAny(const Json::Value &root, std::initializer_list<const char *> keys,
                         const std::string &fallback = {}) {
  for (const char *key : keys) {
    const std::string value = JsonString(root, key);
    if (!value.empty())
      return value;
  }
  return fallback;
}

bool JsonBool(const Json::Value &root, const char *key, bool fallback) {
  if (root.isMember(key) && root[key].isBool())
    return root[key].asBool();
  return fallback;
}

uint64_t JsonUInt64(const Json::Value &root, const char *key, uint64_t fallback) {
  if (root.isMember(key) && root[key].isUInt64())
    return root[key].asUInt64();
  if (root.isMember(key) && root[key].isUInt())
    return root[key].asUInt();
  return fallback;
}

UpdaterConfig LoadUpdaterConfig(const fs::path &path, Role expected_role) {
  UpdaterConfig config;
  Json::Value root;
  std::string error;
  if (!JsonLoadFile(path, &root, &error))
    throw std::runtime_error(error);

  config.repo = JsonString(root, "repo", config.repo);
  config.channel = JsonString(root, "channel", config.channel);
  config.role = JsonString(root, "role", std::string(RoleToCString(expected_role)));
  config.release_index_asset =
      JsonString(root, "release_index_asset", "worr-release-index-" + config.channel + ".json");
  config.launch_exe = JsonStringAny(root, {"launch_exe", "launcher_exe"}, config.launch_exe);
  config.autolaunch = JsonBool(root, "autolaunch", config.autolaunch);
  config.allow_prerelease = JsonBool(root, "allow_prerelease", config.allow_prerelease);
  if (root.isMember("preserve") && root["preserve"].isArray()) {
    config.preserve.clear();
    for (const Json::Value &item : root["preserve"]) {
      if (item.isString())
        config.preserve.push_back(item.asString());
    }
  }
  return config;
}

InstallManifest LoadInstallManifest(const fs::path &path) {
  InstallManifest manifest;
  Json::Value root;
  std::string error;
  if (!JsonLoadFile(path, &root, &error))
    throw std::runtime_error(error);
  manifest.version = JsonString(root, "version");
  manifest.role = JsonString(root, "role");
  manifest.launch_exe = JsonStringAny(root, {"launch_exe", "launcher_exe"});
  manifest.engine_library = JsonStringAny(root, {"engine_library", "runtime_exe"});
  manifest.local_manifest_name = JsonString(root, "local_manifest_name", kLocalManifestName);
  if (root.isMember("files") && root["files"].isArray()) {
    for (const Json::Value &entry : root["files"]) {
      if (!entry.isObject())
        continue;
      FileEntry file;
      file.path = JsonString(entry, "path");
      file.sha256 = JsonString(entry, "sha256");
      file.size = JsonUInt64(entry, "size", 0);
      if (!file.path.empty())
        manifest.files.push_back(std::move(file));
    }
  }
  return manifest;
}

Json::Value RemotePayloadToJson(const RemotePayload &payload) {
  Json::Value root(Json::objectValue);
  root["version"] = payload.version;
  root["tag"] = payload.tag;
  root["role"] = payload.role;
  root["launch_exe"] = payload.launch_exe;
  root["engine_library"] = payload.engine_library;
  root["update_manifest_name"] = payload.update_manifest_name;
  root["update_package_name"] = payload.update_package_name;
  root["local_manifest_name"] = payload.local_manifest_name;
  root["manifest_url"] = payload.manifest_url;
  root["package_url"] = payload.package_url;
  root["manifest"] = payload.manifest_json;
  return root;
}

std::optional<RemotePayload> RemotePayloadFromJson(const Json::Value &root) {
  if (!root.isObject())
    return std::nullopt;

  RemotePayload payload;
  payload.version = JsonString(root, "version");
  payload.tag = JsonString(root, "tag");
  payload.role = JsonString(root, "role");
  payload.launch_exe = JsonStringAny(root, {"launch_exe", "launcher_exe"});
  payload.engine_library = JsonStringAny(root, {"engine_library", "runtime_exe"});
  payload.update_manifest_name = JsonString(root, "update_manifest_name");
  payload.update_package_name = JsonString(root, "update_package_name");
  payload.local_manifest_name = JsonString(root, "local_manifest_name", kLocalManifestName);
  payload.manifest_url = JsonString(root, "manifest_url");
  payload.package_url = JsonString(root, "package_url");
  payload.manifest_json = root["manifest"];
  if (payload.version.empty() || payload.package_url.empty() || payload.manifest_url.empty())
    return std::nullopt;
  return payload;
}

class BootstrapUi {
public:
  virtual ~BootstrapUi() = default;
  virtual bool SetStatus(const std::string &headline, const std::string &detail = {}) = 0;
  virtual bool SetProgress(const std::string &label, uint64_t current, uint64_t total) = 0;
  virtual bool Pump() = 0;
  virtual bool PromptInstall(const std::string &headline, const std::string &detail) = 0;
  virtual void DismissForEngineHandoff() = 0;
};

class ConsoleUi final : public BootstrapUi {
public:
  bool SetStatus(const std::string &headline, const std::string &detail = {}) override {
    if (headline != last_headline_ || detail != last_detail_) {
      std::cout << headline;
      if (!detail.empty())
        std::cout << ": " << detail;
      std::cout << std::endl;
      last_headline_ = headline;
      last_detail_ = detail;
    }
    return true;
  }

  bool SetProgress(const std::string &label, uint64_t current, uint64_t total) override {
    if (total == 0)
      return true;
    const int percent = static_cast<int>((current * 100u) / std::max<uint64_t>(1, total));
    if (label != last_progress_label_ || percent >= last_percent_ + 10 || percent == 100) {
      std::cout << label << ": " << percent << "%" << std::endl;
      last_progress_label_ = label;
      last_percent_ = percent;
    }
    return true;
  }

  bool Pump() override { return true; }

  bool PromptInstall(const std::string &headline, const std::string &detail) override {
    SetStatus(headline, detail);
    std::cout << "[I]nstall or [E]xit? " << std::flush;
    std::string answer;
    while (std::getline(std::cin, answer)) {
      const std::string lowered = ToLower(Trim(answer));
      if (lowered.empty())
        continue;
      if (lowered == "i" || lowered == "install")
        return true;
      if (lowered == "e" || lowered == "exit")
        return false;
      std::cout << "Type Install or Exit: " << std::flush;
    }
    return false;
  }

  void DismissForEngineHandoff() override {}

private:
  std::string last_headline_;
  std::string last_detail_;
  std::string last_progress_label_;
  int last_percent_ = -100;
};

class SilentUi final : public BootstrapUi {
public:
  bool SetStatus(const std::string &, const std::string & = {}) override { return true; }
  bool SetProgress(const std::string &, uint64_t, uint64_t) override { return true; }
  bool Pump() override { return true; }
  bool PromptInstall(const std::string &, const std::string &) override { return false; }
  void DismissForEngineHandoff() override {}
};

std::vector<std::string> WrapText(const std::string &text, size_t width) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string word;
  std::string current;
  while (stream >> word) {
    if (current.empty()) {
      current = word;
    } else if (current.size() + 1 + word.size() <= width) {
      current += " " + word;
    } else {
      lines.push_back(current);
      current = word;
    }
  }
  if (!current.empty())
    lines.push_back(current);
  if (lines.empty())
    lines.push_back({});
  return lines;
}

class SplashUi final : public BootstrapUi {
public:
  SplashUi() {
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
      if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
      owns_sdl_video_ = true;
    }

    window_ = SDL_CreateWindow("WORR", 960, 540, SDL_WINDOW_RESIZABLE);
    if (!window_)
      throw std::runtime_error(SDL_GetError());
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
      throw std::runtime_error(SDL_GetError());

    SDL_Surface *surface = SDL_CreateSurfaceFrom(
        generated::kBootstrapLogoWidth,
        generated::kBootstrapLogoHeight,
        SDL_PIXELFORMAT_RGBA32,
        const_cast<uint8_t *>(generated::kBootstrapLogoRgba),
        generated::kBootstrapLogoWidth * 4);
    if (!surface)
      throw std::runtime_error(SDL_GetError());

    logo_ = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (!logo_)
      throw std::runtime_error(SDL_GetError());
  }

  ~SplashUi() override { ReleaseUiResources(false); }

  bool SetStatus(const std::string &headline, const std::string &detail = {}) override {
    headline_ = headline;
    detail_ = detail;
    Render();
    return !closed_;
  }

  bool SetProgress(const std::string &label, uint64_t current, uint64_t total) override {
    progress_label_ = label;
    progress_current_ = current;
    progress_total_ = total;
    Render();
    return !closed_;
  }

  bool Pump() override {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        closed_ = true;
        return false;
      }
      if (!prompt_active_)
        continue;
      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
          prompt_result_ = true;
        else if (event.key.key == SDLK_ESCAPE)
          prompt_result_ = false;
      } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        const float x = event.button.x;
        const float y = event.button.y;
        if (x >= install_button_.x && x <= install_button_.x + install_button_.w &&
            y >= install_button_.y && y <= install_button_.y + install_button_.h) {
          prompt_result_ = true;
        } else if (x >= exit_button_.x && x <= exit_button_.x + exit_button_.w &&
                   y >= exit_button_.y && y <= exit_button_.y + exit_button_.h) {
          prompt_result_ = false;
        }
      }
    }
    Render();
    SDL_Delay(kUiTickDelayMs);
    return !closed_;
  }

  bool PromptInstall(const std::string &headline, const std::string &detail) override {
    headline_ = headline;
    detail_ = detail;
    prompt_active_ = true;
    prompt_result_.reset();
    while (!prompt_result_.has_value() && Pump()) {
    }
    prompt_active_ = false;
    Render();
    return prompt_result_.value_or(false);
  }

  void DismissForEngineHandoff() override {
    closed_ = true;
    ReleaseUiResources(true);
  }

private:
  static constexpr float kHeadlineTextScale = 2.0f;
  static constexpr float kDetailTextScale = 2.0f;
  static constexpr float kProgressTextScale = 1.5f;
  static constexpr float kButtonTextScale = 1.5f;
  static constexpr float kPanelPadding = 16.0f;
  static constexpr float kPanelBottomMargin = 40.0f;
  static constexpr float kPanelSectionGap = 8.0f;
  static constexpr float kProgressBarHeight = 16.0f;
  static constexpr float kButtonWidth = 132.0f;
  static constexpr float kButtonHeight = 36.0f;
  static constexpr float kButtonGap = 16.0f;

  void ReleaseUiResources(bool keep_video_subsystem) {
    if (logo_)
      SDL_DestroyTexture(logo_);
    if (renderer_)
      SDL_DestroyRenderer(renderer_);
    if (window_)
      SDL_DestroyWindow(window_);
    logo_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
    if (owns_sdl_video_ && !keep_video_subsystem)
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    owns_sdl_video_ = false;
  }

  static float DebugTextPixelSize(float scale) { return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale; }

  static float DebugTextLineAdvance(float scale) { return (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 4.0f) * scale; }

  static size_t DebugTextWrapWidth(float pixel_width, float scale) {
    const float character_width = std::max(1.0f, DebugTextPixelSize(scale));
    return std::max<size_t>(1, static_cast<size_t>(pixel_width / character_width));
  }

  float MeasureTextBlockHeight(const std::string &text, float scale, float max_width) const {
    return static_cast<float>(WrapText(text, DebugTextWrapWidth(max_width, scale)).size()) * DebugTextLineAdvance(scale);
  }

  void DrawScaledDebugText(float x, float y, const std::string &text, float scale) {
    float old_scale_x = 1.0f;
    float old_scale_y = 1.0f;
    SDL_GetRenderScale(renderer_, &old_scale_x, &old_scale_y);
    SDL_SetRenderScale(renderer_, scale, scale);
    SDL_RenderDebugText(renderer_, x / scale, y / scale, text.c_str());
    SDL_SetRenderScale(renderer_, old_scale_x, old_scale_y);
  }

  float DrawTextBlock(float x, float y, const std::string &text, float scale, float max_width) {
    float line_y = y;
    const float line_advance = DebugTextLineAdvance(scale);
    for (const std::string &line : WrapText(text, DebugTextWrapWidth(max_width, scale))) {
      DrawScaledDebugText(x, line_y, line, scale);
      line_y += line_advance;
    }
    return line_y;
  }

  void DrawButton(const SDL_FRect &rect, const std::string &label, bool primary) {
    SDL_SetRenderDrawColor(renderer_, primary ? 159 : 56, primary ? 202 : 64, primary ? 68 : 56, 255);
    SDL_RenderFillRect(renderer_, &rect);
    SDL_SetRenderDrawColor(renderer_, 229, 235, 219, 255);
    SDL_RenderRect(renderer_, &rect);
    const float text_width = static_cast<float>(label.size()) * DebugTextPixelSize(kButtonTextScale);
    const float text_height = DebugTextPixelSize(kButtonTextScale);
    DrawScaledDebugText(rect.x + (rect.w - text_width) * 0.5f, rect.y + (rect.h - text_height) * 0.5f, label,
                        kButtonTextScale);
  }

  void Render() {
    if (closed_ || !renderer_)
      return;

    int width = 0;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &width, &height);

    SDL_SetRenderDrawColor(renderer_, 14, 16, 18, 255);
    SDL_RenderClear(renderer_);

    const float banner_width = static_cast<float>(width) - 80.0f;
    const float banner_height = banner_width * static_cast<float>(generated::kBootstrapLogoHeight) /
                                static_cast<float>(generated::kBootstrapLogoWidth);
    SDL_FRect banner_rect{40.0f, 28.0f, banner_width, std::min<float>(banner_height, height * 0.6f)};
    SDL_RenderTexture(renderer_, logo_, nullptr, &banner_rect);

    const float panel_width = static_cast<float>(width) - 80.0f;
    const float panel_text_width = panel_width - (kPanelPadding * 2.0f);
    const float headline_height = MeasureTextBlockHeight(headline_, kHeadlineTextScale, panel_text_width);
    const float detail_height = MeasureTextBlockHeight(detail_, kDetailTextScale, panel_text_width);
    const float progress_label_height = DebugTextPixelSize(kProgressTextScale);
    const float prompt_section_height =
        prompt_active_ ? (kPanelSectionGap + kButtonHeight + kPanelPadding) : kPanelPadding;
    const float panel_height = kPanelPadding + headline_height + kPanelSectionGap + detail_height +
                               kPanelSectionGap + progress_label_height + 6.0f + kProgressBarHeight +
                               prompt_section_height;
    SDL_FRect panel_rect{40.0f, std::max(40.0f, static_cast<float>(height) - panel_height - kPanelBottomMargin),
                         panel_width, panel_height};
    SDL_SetRenderDrawColor(renderer_, 22, 26, 28, 220);
    SDL_RenderFillRect(renderer_, &panel_rect);
    SDL_SetRenderDrawColor(renderer_, 110, 130, 104, 255);
    SDL_RenderRect(renderer_, &panel_rect);

    const float text_x = panel_rect.x + kPanelPadding;
    float cursor_y = panel_rect.y + kPanelPadding;
    cursor_y = DrawTextBlock(text_x, cursor_y, headline_, kHeadlineTextScale, panel_text_width);
    cursor_y += 2.0f;
    cursor_y = DrawTextBlock(text_x, cursor_y, detail_, kDetailTextScale, panel_text_width);
    cursor_y += kPanelSectionGap;

    DrawScaledDebugText(text_x, cursor_y, progress_label_, kProgressTextScale);
    SDL_FRect bar_back{text_x, cursor_y + progress_label_height + 6.0f, panel_rect.w - (kPanelPadding * 2.0f),
                       kProgressBarHeight};
    SDL_SetRenderDrawColor(renderer_, 40, 46, 40, 255);
    SDL_RenderFillRect(renderer_, &bar_back);
    SDL_SetRenderDrawColor(renderer_, 72, 88, 64, 255);
    SDL_RenderRect(renderer_, &bar_back);

    float progress = 0.0f;
    if (progress_total_ > 0) {
      progress = static_cast<float>(progress_current_) / static_cast<float>(progress_total_);
    } else {
      const uint64_t tick = SDL_GetTicks();
      progress = static_cast<float>((tick % 2000u)) / 2000.0f;
    }
    progress = std::clamp(progress, 0.0f, 1.0f);
    SDL_FRect bar_fill{bar_back.x + 2.0f, bar_back.y + 2.0f, (bar_back.w - 4.0f) * progress, bar_back.h - 4.0f};
    SDL_SetRenderDrawColor(renderer_, 155, 198, 66, 255);
    SDL_RenderFillRect(renderer_, &bar_fill);

    if (prompt_active_) {
      const float button_y = bar_back.y + bar_back.h + kPanelSectionGap;
      exit_button_ = SDL_FRect{panel_rect.x + panel_rect.w - kPanelPadding - kButtonWidth, button_y, kButtonWidth,
                               kButtonHeight};
      install_button_ = SDL_FRect{exit_button_.x - kButtonGap - kButtonWidth, button_y, kButtonWidth, kButtonHeight};
      DrawButton(install_button_, "Install", true);
      DrawButton(exit_button_, "Exit", false);
    }

    SDL_RenderPresent(renderer_);
  }

  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *logo_ = nullptr;
  bool owns_sdl_video_ = false;
  bool closed_ = false;
  bool prompt_active_ = false;
  std::optional<bool> prompt_result_;
  std::string headline_;
  std::string detail_;
  std::string progress_label_ = "Starting";
  uint64_t progress_current_ = 0;
  uint64_t progress_total_ = 0;
  SDL_FRect install_button_{};
  SDL_FRect exit_button_{};
};

class UiHandle {
public:
  UiHandle(Role role, bool quiet_status) {
    if (quiet_status) {
      ui_ = std::make_unique<SilentUi>();
      return;
    }
    if (role == Role::Client) {
      try {
        ui_ = std::make_unique<SplashUi>();
      } catch (...) {
        ui_ = std::make_unique<ConsoleUi>();
      }
    } else {
      ui_ = std::make_unique<ConsoleUi>();
    }
  }

  BootstrapUi *operator->() { return ui_.get(); }
  BootstrapUi &operator*() { return *ui_; }
  BootstrapUi *Get() { return ui_.get(); }
  void DismissForEngineHandoff() {
    if (ui_)
      ui_->DismissForEngineHandoff();
  }

private:
  std::unique_ptr<BootstrapUi> ui_;
};

struct CurlProgressContext {
  BootstrapUi *ui = nullptr;
  std::string label;
};

size_t CurlWriteToString(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t CurlWriteToFile(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *file = static_cast<FILE *>(userdata);
  return std::fwrite(ptr, size, nmemb, file);
}

int CurlProgressThunk(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
  auto *ctx = static_cast<CurlProgressContext *>(userdata);
  if (ctx->ui) {
    if (!ctx->ui->SetProgress(ctx->label, static_cast<uint64_t>(dlnow), static_cast<uint64_t>(dltotal)))
      return 1;
    if (!ctx->ui->Pump())
      return 1;
  }
  return 0;
}

int RemainingBudgetMs(const clock_type::time_point &deadline) {
  const auto now = clock_type::now();
  if (now >= deadline)
    return 0;
  return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

std::string HttpGetString(const std::string &url, int timeout_ms, BootstrapUi *ui, const std::string &label) {
  CURL *curl = curl_easy_init();
  if (!curl)
    throw std::runtime_error("curl_easy_init failed");

  std::string response;
  CurlProgressContext progress{ui, label};
  curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
  headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressThunk);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

  const CURLcode result = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  if (result != CURLE_OK)
    throw std::runtime_error(curl_easy_strerror(result));
  return response;
}

void HttpDownloadFile(const std::string &url, const fs::path &path, int timeout_ms, BootstrapUi *ui,
                      const std::string &label) {
  fs::create_directories(path.parent_path());
  FILE *file = std::fopen(path.string().c_str(), "wb");
  if (!file)
    throw std::runtime_error("Could not open " + GenericPath(path));

  CURL *curl = curl_easy_init();
  if (!curl) {
    std::fclose(file);
    throw std::runtime_error("curl_easy_init failed");
  }

  CurlProgressContext progress{ui, label};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToFile);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressThunk);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

  const CURLcode result = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  std::fclose(file);

  if (result != CURLE_OK) {
    std::error_code ignored;
    fs::remove(path, ignored);
    throw std::runtime_error(curl_easy_strerror(result));
  }
}

Json::Value ParseJsonString(const std::string &text, const std::string &context) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream stream(text);
  if (!Json::parseFromStream(builder, stream, &root, &errors))
    throw std::runtime_error("Failed parsing " + context + ": " + errors);
  return root;
}

std::vector<ReleaseAsset> ReleaseAssetsFromJson(const Json::Value &release) {
  std::vector<ReleaseAsset> assets;
  if (!release.isMember("assets") || !release["assets"].isArray())
    return assets;
  for (const Json::Value &asset : release["assets"]) {
    ReleaseAsset item;
    item.name = JsonString(asset, "name");
    item.url = JsonString(asset, "browser_download_url");
    if (!item.name.empty() && !item.url.empty())
      assets.push_back(std::move(item));
  }
  return assets;
}

std::optional<ReleaseAsset> FindAssetByName(const std::vector<ReleaseAsset> &assets, const std::string &name) {
  for (const ReleaseAsset &asset : assets) {
    if (asset.name == name)
      return asset;
  }
  return std::nullopt;
}

std::string CanonicalTagVersion(const std::string &tag) {
  if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
    return tag.substr(1);
  return tag;
}

bool MatchesChannel(const std::string &tag, const std::string &channel) {
  const bool nightly_tag = tag.rfind("nightly-", 0) == 0;
  if (ToLower(channel) == "nightly")
    return nightly_tag;
  return !nightly_tag;
}

Json::Value SelectRelease(const Json::Value &releases, const UpdaterConfig &config) {
  if (!releases.isArray())
    throw std::runtime_error("GitHub releases payload was not an array");

  for (const Json::Value &release : releases) {
    if (!release.isObject())
      continue;
    const std::string tag = JsonString(release, "tag_name");
    if (!MatchesChannel(tag, config.channel))
      continue;

    const bool prerelease = JsonBool(release, "prerelease", false);
    if (ToLower(config.channel) != "nightly" && !config.allow_prerelease) {
      const std::string version = CanonicalTagVersion(tag);
      if (prerelease || IsPrereleaseVersion(version))
        continue;
    }

    return release;
  }

  throw std::runtime_error("No matching GitHub release found for channel " + config.channel);
}

FileEntry FileEntryFromJson(const Json::Value &value) {
  FileEntry entry;
  entry.path = JsonString(value, "path");
  entry.sha256 = JsonString(value, "sha256");
  entry.size = JsonUInt64(value, "size", 0);
  return entry;
}

std::vector<FileEntry> ManifestFiles(const Json::Value &manifest_json) {
  std::vector<FileEntry> files;
  if (!manifest_json.isMember("files") || !manifest_json["files"].isArray())
    return files;
  for (const Json::Value &value : manifest_json["files"]) {
    FileEntry entry = FileEntryFromJson(value);
    if (!entry.path.empty())
      files.push_back(std::move(entry));
  }
  return files;
}

std::string CurrentPlatformId() {
#if defined(_WIN32)
  return "windows-x86_64";
#elif defined(__APPLE__)
  return "macos-x86_64";
#else
  return "linux-x86_64";
#endif
}

RemotePayload DiscoverRemotePayload(const UpdaterConfig &config, Role role, const InstallManifest &local_manifest,
                                    BootstrapUi *ui, const clock_type::time_point &deadline) {
  const int releases_timeout = std::max(1000, RemainingBudgetMs(deadline));
  const std::string releases_url = "https://api.github.com/repos/" + config.repo + "/releases";
  const Json::Value releases_json =
      ParseJsonString(HttpGetString(releases_url, releases_timeout, ui, "Checking release feed"), "release feed");
  const Json::Value release_json = SelectRelease(releases_json, config);
  const std::vector<ReleaseAsset> release_assets = ReleaseAssetsFromJson(release_json);

  const auto index_asset = FindAssetByName(release_assets, config.release_index_asset);
  if (!index_asset)
    throw std::runtime_error("Release is missing asset " + config.release_index_asset);

  const int index_timeout = std::max(1000, RemainingBudgetMs(deadline));
  const Json::Value index_json =
      ParseJsonString(HttpGetString(index_asset->url, index_timeout, ui, "Loading release index"), "release index");

  const std::string remote_version = JsonString(index_json, "version");
  if (CompareSemver(local_manifest.version, remote_version) >= 0) {
    RemotePayload current;
    current.version = remote_version;
    return current;
  }

  const Json::Value targets = index_json["targets"];
  Json::Value selected_target;
  for (const Json::Value &target : targets) {
    if (JsonString(target, "platform_id") == CurrentPlatformId()) {
      selected_target = target;
      break;
    }
  }
  if (selected_target.isNull())
    throw std::runtime_error("Release index does not contain a target for " + CurrentPlatformId());

  const std::string role_name = RoleToCString(role);
  const Json::Value roles = selected_target["roles"];
  if (!roles.isObject() || !roles.isMember(role_name))
    throw std::runtime_error("Release index does not contain role metadata for " + role_name);
  const Json::Value role_json = roles[role_name];

  const std::string manifest_name = JsonString(role_json, "update_manifest_name");
  const std::string package_name = JsonString(role_json, "update_package_name");
  const auto manifest_asset = FindAssetByName(release_assets, manifest_name);
  const auto package_asset = FindAssetByName(release_assets, package_name);
  if (!manifest_asset)
    throw std::runtime_error("Release is missing asset " + manifest_name);
  if (!package_asset)
    throw std::runtime_error("Release is missing asset " + package_name);

  const int manifest_timeout = std::max(1000, RemainingBudgetMs(deadline));
  const Json::Value manifest_json =
      ParseJsonString(HttpGetString(manifest_asset->url, manifest_timeout, ui, "Loading update manifest"),
                      "update manifest");

  RemotePayload payload;
  payload.version = JsonString(manifest_json, "version", remote_version);
  payload.tag = JsonString(release_json, "tag_name");
  payload.role = role_name;
  payload.launch_exe = JsonStringAny(role_json, {"launch_exe", "launcher_exe"}, DefaultLaunchRelpath(role));
  payload.engine_library =
      JsonStringAny(role_json, {"engine_library", "runtime_exe"}, DefaultEngineLibraryRelpath(role));
  payload.update_manifest_name = manifest_name;
  payload.update_package_name = package_name;
  payload.local_manifest_name = JsonString(role_json, "local_manifest_name", kLocalManifestName);
  payload.manifest_url = manifest_asset->url;
  payload.package_url = package_asset->url;
  payload.manifest_json = manifest_json;
  return payload;
}

class Sha256 {
public:
  Sha256() { Reset(); }

  void Reset() {
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

  void Update(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      data_[data_length_] = data[i];
      ++data_length_;
      if (data_length_ == 64) {
        Transform();
        bit_length_ += 512;
        data_length_ = 0;
      }
    }
  }

  std::array<uint8_t, 32> Final() {
    uint32_t i = data_length_;

    if (data_length_ < 56) {
      data_[i++] = 0x80;
      while (i < 56)
        data_[i++] = 0x00;
    } else {
      data_[i++] = 0x80;
      while (i < 64)
        data_[i++] = 0x00;
      Transform();
      std::fill(data_.begin(), data_.begin() + 56, 0);
    }

    bit_length_ += static_cast<uint64_t>(data_length_) * 8u;
    data_[63] = static_cast<uint8_t>(bit_length_);
    data_[62] = static_cast<uint8_t>(bit_length_ >> 8u);
    data_[61] = static_cast<uint8_t>(bit_length_ >> 16u);
    data_[60] = static_cast<uint8_t>(bit_length_ >> 24u);
    data_[59] = static_cast<uint8_t>(bit_length_ >> 32u);
    data_[58] = static_cast<uint8_t>(bit_length_ >> 40u);
    data_[57] = static_cast<uint8_t>(bit_length_ >> 48u);
    data_[56] = static_cast<uint8_t>(bit_length_ >> 56u);
    Transform();

    std::array<uint8_t, 32> hash{};
    for (i = 0; i < 4; ++i) {
      hash[i] = static_cast<uint8_t>((state_[0] >> (24 - i * 8)) & 0xff);
      hash[i + 4] = static_cast<uint8_t>((state_[1] >> (24 - i * 8)) & 0xff);
      hash[i + 8] = static_cast<uint8_t>((state_[2] >> (24 - i * 8)) & 0xff);
      hash[i + 12] = static_cast<uint8_t>((state_[3] >> (24 - i * 8)) & 0xff);
      hash[i + 16] = static_cast<uint8_t>((state_[4] >> (24 - i * 8)) & 0xff);
      hash[i + 20] = static_cast<uint8_t>((state_[5] >> (24 - i * 8)) & 0xff);
      hash[i + 24] = static_cast<uint8_t>((state_[6] >> (24 - i * 8)) & 0xff);
      hash[i + 28] = static_cast<uint8_t>((state_[7] >> (24 - i * 8)) & 0xff);
    }
    return hash;
  }

private:
  static constexpr std::array<uint32_t, 64> kTable = {
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

  static uint32_t RotateRight(uint32_t value, uint32_t bits) { return (value >> bits) | (value << (32u - bits)); }

  void Transform() {
    uint32_t m[64];
    for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4) {
      m[i] = (static_cast<uint32_t>(data_[j]) << 24u) | (static_cast<uint32_t>(data_[j + 1]) << 16u) |
             (static_cast<uint32_t>(data_[j + 2]) << 8u) | static_cast<uint32_t>(data_[j + 3]);
    }
    for (uint32_t i = 16; i < 64; ++i) {
      const uint32_t s0 = RotateRight(m[i - 15], 7u) ^ RotateRight(m[i - 15], 18u) ^ (m[i - 15] >> 3u);
      const uint32_t s1 = RotateRight(m[i - 2], 17u) ^ RotateRight(m[i - 2], 19u) ^ (m[i - 2] >> 10u);
      m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (uint32_t i = 0; i < 64; ++i) {
      const uint32_t s1 = RotateRight(e, 6u) ^ RotateRight(e, 11u) ^ RotateRight(e, 25u);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t temp1 = h + s1 + ch + kTable[i] + m[i];
      const uint32_t s0 = RotateRight(a, 2u) ^ RotateRight(a, 13u) ^ RotateRight(a, 22u);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + maj;

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

  std::array<uint8_t, 64> data_{};
  uint32_t data_length_ = 0;
  uint64_t bit_length_ = 0;
  std::array<uint32_t, 8> state_{};
};

std::string Sha256File(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("Could not open " + GenericPath(path));

  Sha256 sha;
  std::array<char, 1 << 15> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = file.gcount();
    if (count > 0)
      sha.Update(reinterpret_cast<const uint8_t *>(buffer.data()), static_cast<size_t>(count));
  }

  const auto digest = sha.Final();
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (uint8_t byte : digest)
    ss << std::setw(2) << static_cast<int>(byte);
  return ss.str();
}

void ExtractZip(const fs::path &archive_path, const fs::path &stage_dir, BootstrapUi *ui) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, archive_path.string().c_str(), 0))
    throw std::runtime_error("Could not open zip " + GenericPath(archive_path));

  const mz_uint file_count = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < file_count; ++i) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&zip, i, &stat))
      continue;

    const fs::path target = stage_dir / Utf8Path(stat.m_filename);
    if (mz_zip_reader_is_file_a_directory(&zip, i)) {
      fs::create_directories(target);
      continue;
    }

    fs::create_directories(target.parent_path());
    if (!mz_zip_reader_extract_to_file(&zip, i, target.string().c_str(), 0)) {
      mz_zip_reader_end(&zip);
      throw std::runtime_error("Failed extracting " + std::string(stat.m_filename));
    }

    if (ui) {
      ui->SetProgress("Extracting update payload", static_cast<uint64_t>(i + 1), static_cast<uint64_t>(file_count));
      if (!ui->Pump()) {
        mz_zip_reader_end(&zip);
        throw std::runtime_error("Extraction cancelled");
      }
    }
  }

  mz_zip_reader_end(&zip);
}

bool IsPreserved(const std::string &rel_path, const UpdaterConfig &config) {
  for (const std::string &pattern : config.preserve) {
    if (MatchesPattern(pattern, rel_path))
      return true;
  }
  return false;
}

Json::Value BuildLocalInstallManifest(const RemotePayload &payload) {
  Json::Value root = payload.manifest_json;
  root.removeMember("package");
  root["install_state"] = true;
  root["launch_exe"] = payload.launch_exe;
  root["engine_library"] = payload.engine_library;
  root["local_manifest_name"] = payload.local_manifest_name;
  return root;
}

void ApplyPayload(const fs::path &stage_dir, const fs::path &install_root, const UpdaterConfig &config,
                  const RemotePayload &payload, BootstrapUi *ui) {
  const std::vector<FileEntry> files = ManifestFiles(payload.manifest_json);
  if (files.empty())
    throw std::runtime_error("Update manifest contained no files");

  for (const FileEntry &file : files) {
    const fs::path staged = stage_dir / Utf8Path(file.path);
    if (!fs::is_regular_file(staged))
      throw std::runtime_error("Missing staged file " + file.path);
    if (!file.sha256.empty() && ToLower(Sha256File(staged)) != ToLower(file.sha256))
      throw std::runtime_error("Hash mismatch for staged file " + file.path);
  }

  const fs::path rollback_root = fs::temp_directory_path() / ("worr-update-rollback-" + RandomToken());
  fs::create_directories(rollback_root);

  std::set<std::string> expected_paths;
  for (const FileEntry &file : files)
    expected_paths.insert(FoldPathCase(file.path));

  std::vector<std::string> backup_paths;
  std::vector<std::string> created_paths;

  auto backup_file = [&](const std::string &rel_path) {
    const fs::path source = install_root / Utf8Path(rel_path);
    if (!fs::exists(source))
      return;
    const fs::path dest = rollback_root / Utf8Path(rel_path);
    fs::create_directories(dest.parent_path());
    fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
    backup_paths.push_back(rel_path);
  };

  auto rollback = [&]() {
    for (const std::string &rel_path : created_paths) {
      std::error_code ignored;
      fs::remove(install_root / Utf8Path(rel_path), ignored);
    }
    for (const std::string &rel_path : backup_paths) {
      const fs::path backup = rollback_root / Utf8Path(rel_path);
      const fs::path dest = install_root / Utf8Path(rel_path);
      fs::create_directories(dest.parent_path());
      std::error_code ignored;
      fs::copy_file(backup, dest, fs::copy_options::overwrite_existing, ignored);
    }
  };

  try {
    const uint64_t total_steps = static_cast<uint64_t>(files.size());
    uint64_t step = 0;
    for (const FileEntry &file : files) {
      const std::string rel_path = file.path;
      const bool preserved = IsPreserved(rel_path, config);
      const fs::path live_path = install_root / Utf8Path(rel_path);
      const fs::path staged_path = stage_dir / Utf8Path(rel_path);
      if (!preserved || !fs::exists(live_path)) {
        if (fs::exists(live_path))
          backup_file(rel_path);
        else
          created_paths.push_back(rel_path);
        fs::create_directories(live_path.parent_path());
        fs::copy_file(staged_path, live_path, fs::copy_options::overwrite_existing);
      }

      ++step;
      if (ui) {
        ui->SetProgress("Installing update", step, total_steps);
        if (!ui->Pump())
          throw std::runtime_error("Install cancelled");
      }
    }

    std::vector<fs::path> live_files;
    for (const fs::path &path : fs::recursive_directory_iterator(install_root)) {
      if (fs::is_regular_file(path))
        live_files.push_back(path);
    }

    for (const fs::path &path : live_files) {
      if (!fs::is_regular_file(path))
        continue;
      const std::string rel_path = NormalizeRelativePath(fs::relative(path, install_root));
      if (expected_paths.count(rel_path) != 0)
        continue;
      if (IsPreserved(rel_path, config))
        continue;
      backup_file(rel_path);
      fs::remove(path);
    }

    std::string error;
    const Json::Value local_manifest = BuildLocalInstallManifest(payload);
    if (!JsonWriteFile(install_root / payload.local_manifest_name, local_manifest, &error))
      throw std::runtime_error(error);
  } catch (...) {
    rollback();
    std::error_code ignored;
    fs::remove_all(rollback_root, ignored);
    throw;
  }

  std::error_code ignored;
  fs::remove_all(rollback_root, ignored);
}

fs::path BasePathFromExecutable() {
  const char *base = SDL_GetBasePath();
  if (!base)
    throw std::runtime_error(SDL_GetError());
  return NormalizeInstallRoot(Utf8Path(base));
}

std::vector<char *> BuildArgPointers(std::vector<std::string> &args) {
  std::vector<char *> pointers;
  pointers.reserve(args.size() + 1);
  for (std::string &arg : args)
    pointers.push_back(arg.data());
  pointers.push_back(nullptr);
  return pointers;
}

SDL_Process *SpawnProcess(const fs::path &working_dir, std::vector<std::string> args,
                          const std::vector<std::pair<std::string, std::string>> &env,
                          bool background) {
  std::vector<char *> arg_ptrs = BuildArgPointers(args);
  SDL_PropertiesID props = SDL_CreateProperties();
  if (!props)
    throw std::runtime_error(SDL_GetError());

  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arg_ptrs.data());
  SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, GenericPath(working_dir).c_str());
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, background);
  if (background) {
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
  }

  SDL_Environment *environment = nullptr;
  if (!env.empty()) {
    environment = SDL_CreateEnvironment(true);
    if (!environment) {
      SDL_DestroyProperties(props);
      throw std::runtime_error(SDL_GetError());
    }
    for (const auto &pair : env) {
      SDL_SetEnvironmentVariable(environment, pair.first.c_str(), pair.second.c_str(), true);
    }
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
  }

  SDL_Process *process = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if (environment)
    SDL_DestroyEnvironment(environment);
  if (!process)
    throw std::runtime_error(SDL_GetError());
  return process;
}

#if defined(_WIN32)
bool AttachToParentConsole() {
  if (AttachConsole(ATTACH_PARENT_PROCESS) || GetLastError() == ERROR_ACCESS_DENIED) {
    FILE *dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    freopen_s(&dummy, "CONIN$", "r", stdin);
    return true;
  }
  return false;
}

bool IsProcessElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return false;
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
  CloseHandle(token);
  return ok && elevation.TokenIsElevated != 0;
}

bool IsPermissionDeniedError(const std::error_code &error) {
  if (error == std::errc::permission_denied)
    return true;
  if (error.default_error_condition() == std::errc::permission_denied)
    return true;
  return error.value() == ERROR_ACCESS_DENIED || error.value() == ERROR_SHARING_VIOLATION ||
         error.value() == ERROR_LOCK_VIOLATION;
}

std::wstring Utf8ToWide(const std::string &text) {
  if (text.empty())
    return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
  if (!result.empty())
    result.pop_back();
  return result;
}

std::string WideToUtf8(const wchar_t *text) {
  if (!text || !*text)
    return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
  if (!result.empty())
    result.pop_back();
  return result;
}

std::wstring QuoteWindowsArg(const std::string &arg) {
  const std::wstring wide = Utf8ToWide(arg);
  if (wide.find_first_of(L" \t\"") == std::wstring::npos)
    return wide;
  std::wstring out = L"\"";
  for (wchar_t ch : wide) {
    if (ch == L'"')
      out.push_back(L'\\');
    out.push_back(ch);
  }
  out.push_back(L'"');
  return out;
}

std::wstring BuildWindowsCommandLine(const std::vector<std::string> &args) {
  std::wstring command_line;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i)
      command_line.push_back(L' ');
    command_line += QuoteWindowsArg(args[i]);
  }
  return command_line;
}

HANDLE DuplicateInheritedHandle(HANDLE handle) {
  if (!handle || handle == INVALID_HANDLE_VALUE)
    return INVALID_HANDLE_VALUE;

  HANDLE duplicate = INVALID_HANDLE_VALUE;
  if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
    throw std::runtime_error("Failed duplicating process I/O handle");
  return duplicate;
}

void CloseIfValid(HANDLE handle) {
  if (handle && handle != INVALID_HANDLE_VALUE)
    CloseHandle(handle);
}

void LaunchWindowsProcess(const fs::path &working_dir, const std::vector<std::string> &args, bool background,
                          bool new_process_group) {
  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;

  HANDLE stdin_handle = INVALID_HANDLE_VALUE;
  HANDLE stdout_handle = INVALID_HANDLE_VALUE;
  HANDLE stderr_handle = INVALID_HANDLE_VALUE;
  SECURITY_ATTRIBUTES security_attributes{};
  security_attributes.nLength = sizeof(security_attributes);
  security_attributes.bInheritHandle = TRUE;

  if (background) {
    stdin_handle = CreateFileW(L"\\\\.\\NUL", GENERIC_READ | GENERIC_WRITE, 0, &security_attributes,
                               OPEN_EXISTING, 0, nullptr);
    stdout_handle = CreateFileW(L"\\\\.\\NUL", GENERIC_READ | GENERIC_WRITE, 0, &security_attributes,
                                OPEN_EXISTING, 0, nullptr);
    stderr_handle = CreateFileW(L"\\\\.\\NUL", GENERIC_READ | GENERIC_WRITE, 0, &security_attributes,
                                OPEN_EXISTING, 0, nullptr);
  } else {
    stdin_handle =
        CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &security_attributes, OPEN_EXISTING, 0, nullptr);
    stdout_handle =
        CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &security_attributes, OPEN_EXISTING, 0, nullptr);
    stderr_handle =
        CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &security_attributes, OPEN_EXISTING, 0, nullptr);
    if (stdin_handle == INVALID_HANDLE_VALUE || stdout_handle == INVALID_HANDLE_VALUE ||
        stderr_handle == INVALID_HANDLE_VALUE) {
      CloseIfValid(stdin_handle);
      CloseIfValid(stdout_handle);
      CloseIfValid(stderr_handle);
      stdin_handle = DuplicateInheritedHandle(GetStdHandle(STD_INPUT_HANDLE));
      stdout_handle = DuplicateInheritedHandle(GetStdHandle(STD_OUTPUT_HANDLE));
      stderr_handle = DuplicateInheritedHandle(GetStdHandle(STD_ERROR_HANDLE));
    }
  }

  startup_info.hStdInput = stdin_handle;
  startup_info.hStdOutput = stdout_handle;
  startup_info.hStdError = stderr_handle;

  const std::wstring cwd = working_dir.wstring();
  std::wstring command_line = BuildWindowsCommandLine(args);
  PROCESS_INFORMATION process_info{};
  DWORD creation_flags = CREATE_UNICODE_ENVIRONMENT;
  if (background)
    creation_flags |= CREATE_NO_WINDOW;
  if (new_process_group)
    creation_flags |= CREATE_NEW_PROCESS_GROUP;

  const BOOL ok = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, creation_flags, nullptr,
                                 cwd.c_str(), &startup_info, &process_info);
  const DWORD create_error = ok ? ERROR_SUCCESS : GetLastError();

  CloseIfValid(stdin_handle);
  CloseIfValid(stdout_handle);
  CloseIfValid(stderr_handle);

  if (!ok)
    throw std::runtime_error("CreateProcess failed: " + std::to_string(create_error));

  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
}

bool RelaunchElevated(const fs::path &exe_path, const std::vector<std::string> &args, const fs::path &working_dir) {
  std::wstring parameters;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i)
      parameters.push_back(L' ');
    parameters += QuoteWindowsArg(args[i]);
  }

  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.lpVerb = L"runas";
  const std::wstring exe = exe_path.wstring();
  const std::wstring cwd = working_dir.wstring();
  info.lpFile = exe.c_str();
  info.lpParameters = parameters.c_str();
  info.lpDirectory = cwd.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info))
    return false;
  if (info.hProcess)
    CloseHandle(info.hProcess);
  return true;
}
#endif

void BootstrapReadyDismiss(void *userdata) {
  auto *ui = static_cast<UiHandle *>(userdata);
  if (ui)
    ui->DismissForEngineHandoff();
}

void SetProcessEnvVar(const char *name, const std::string &value) {
#if defined(_WIN32)
  if (!SetEnvironmentVariableW(Utf8ToWide(name).c_str(), Utf8ToWide(value).c_str()))
    throw std::runtime_error("Failed setting environment variable " + std::string(name));
#else
  if (setenv(name, value.c_str(), 1) != 0)
    throw std::runtime_error("Failed setting environment variable " + std::string(name));
#endif
}

void ClearProcessEnvVar(const char *name) {
#if defined(_WIN32)
  SetEnvironmentVariableW(Utf8ToWide(name).c_str(), nullptr);
#else
  unsetenv(name);
#endif
}

int LaunchEngineAndWait(const fs::path &install_root, const std::string &launch_relpath,
                        const std::string &engine_library_relpath,
                        const std::vector<std::string> &forwarded_args, UiHandle *ui) {
  const fs::path engine_path = install_root / Utf8Path(engine_library_relpath);
  if (!fs::is_regular_file(engine_path))
    throw std::runtime_error("Engine library not found: " + GenericPath(engine_path));

  if (ui && ui->Get())
    ui->Get()->SetStatus("Launching WORR", "Starting the hosted engine library.");

  auto *engine_object = SDL_LoadObject(GenericPath(engine_path).c_str());
  if (!engine_object)
    throw std::runtime_error("Could not load engine library " + GenericPath(engine_path) + ": " + SDL_GetError());

  auto *engine_main = reinterpret_cast<engine_main_fn>(SDL_LoadFunction(engine_object, kEngineEntryPoint));
  if (!engine_main) {
    const std::string error = SDL_GetError();
    SDL_UnloadObject(engine_object);
    throw std::runtime_error("Engine library is missing " + std::string(kEngineEntryPoint) + ": " + error);
  }

  auto *set_ready_callback =
      reinterpret_cast<set_ready_callback_fn>(SDL_LoadFunction(engine_object, kReadyCallbackEntryPoint));
  if (!set_ready_callback && ui)
    ui->DismissForEngineHandoff();
  else if (set_ready_callback && ui && ui->Get())
    set_ready_callback(BootstrapReadyDismiss, ui);

  std::vector<std::string> args;
  args.push_back(GenericPath(install_root / Utf8Path(launch_relpath)));
  args.insert(args.end(), forwarded_args.begin(), forwarded_args.end());
  std::vector<char *> arg_ptrs = BuildArgPointers(args);

  SetProcessEnvVar(kBaseDirEnv, GenericPath(install_root));

  int exit_code = 0;
  try {
    exit_code = engine_main(static_cast<int>(args.size()), arg_ptrs.data());
  } catch (...) {
    if (set_ready_callback)
      set_ready_callback(nullptr, nullptr);
    SDL_UnloadObject(engine_object);
    ClearProcessEnvVar(kBaseDirEnv);
    throw;
  }

  if (set_ready_callback)
    set_ready_callback(nullptr, nullptr);
  SDL_UnloadObject(engine_object);
  ClearProcessEnvVar(kBaseDirEnv);
  return exit_code;
}

std::vector<std::string> BuildWorkerInvocation(const BootstrapOptions &options, bool approved_install) {
  std::vector<std::string> args = {
      "--bootstrap",
      "--role",
      std::string(RoleToCString(options.role)),
      "--install-root",
      GenericPath(options.install_root),
      "--launch-rel",
      options.launch_relpath,
      "--engine-rel",
      options.engine_library_relpath,
  };
  if (approved_install)
    args.push_back("--approved-install");
  if (options.quiet_status)
    args.push_back(kQuietStatusArg);
  if (!options.forwarded_args.empty()) {
    args.push_back("--");
    args.insert(args.end(), options.forwarded_args.begin(), options.forwarded_args.end());
  }
  BootstrapTrace("BuildWorkerInvocation approved_install=" + std::to_string(approved_install ? 1 : 0) +
                 " quiet_status=" + std::to_string(options.quiet_status ? 1 : 0) +
                 " argc=" + std::to_string(args.size()));
  return args;
}

int LaunchApprovedUpdateWorker(const BootstrapOptions &options) {
  BootstrapTrace("LaunchApprovedUpdateWorker role=" + std::string(RoleToCString(options.role)) +
                 " install_root=" + GenericPath(options.install_root));
  const fs::path updater_path = options.worker_exe_path.empty() ? options.install_root / kUpdaterStem
                                                                : options.worker_exe_path;
  if (!fs::is_regular_file(updater_path))
    throw std::runtime_error("Updater worker not found: " + GenericPath(updater_path));

  const fs::path temp_worker =
      fs::temp_directory_path() / ("worr-updater-" + RandomToken() + updater_path.extension().string());
  fs::copy_file(updater_path, temp_worker, fs::copy_options::overwrite_existing);
  BootstrapTrace("LaunchApprovedUpdateWorker copied temp_worker=" + GenericPath(temp_worker));

  std::vector<std::string> args;
  args.push_back(GenericPath(temp_worker));
  BootstrapOptions worker_options = options;
#if defined(_WIN32)
  if (options.role == Role::Server)
    worker_options.quiet_status = true;
#endif
  const std::vector<std::string> invocation = BuildWorkerInvocation(worker_options, true);
  args.insert(args.end(), invocation.begin(), invocation.end());
  const bool background = options.role == Role::Client;
#if defined(_WIN32)
  if (options.role == Role::Server) {
    LaunchWindowsProcess(options.install_root, args, false, true);
    BootstrapTrace("LaunchApprovedUpdateWorker spawned temp worker native new_process_group=1");
    return 0;
  }
#endif
  SDL_Process *process = SpawnProcess(options.install_root, std::move(args), {}, background);
  BootstrapTrace("LaunchApprovedUpdateWorker spawned temp worker background=" + std::to_string(background ? 1 : 0));
  SDL_DestroyProcess(process);
  return 0;
}

int RelaunchInstalledBootstrap(const BootstrapOptions &options) {
  const fs::path launch_path = options.install_root / Utf8Path(options.launch_relpath);
  if (!fs::is_regular_file(launch_path))
    throw std::runtime_error("Bootstrap executable not found: " + GenericPath(launch_path));

  std::vector<std::string> args;
  args.push_back(GenericPath(launch_path));
  args.push_back(kSkipUpdateCheckArg);
  args.insert(args.end(), options.forwarded_args.begin(), options.forwarded_args.end());

  const bool background = options.role == Role::Client;
#if defined(_WIN32)
  if (options.role == Role::Server) {
    args.push_back(kQuietStatusArg);
    LaunchWindowsProcess(options.install_root, args, false, true);
    return 0;
  }
#endif
  SDL_Process *process = SpawnProcess(options.install_root, std::move(args), {}, background);
  SDL_DestroyProcess(process);
  return 0;
}

RemotePayload LoadPendingUpdate(const fs::path &state_path, bool *found) {
  *found = false;
  Json::Value root;
  std::string error;
  if (!JsonLoadFile(state_path, &root, &error))
    return {};
  const auto pending = RemotePayloadFromJson(root["pending_update"]);
  if (!pending)
    return {};
  *found = true;
  return *pending;
}

void SaveState(const fs::path &state_path, const std::string &result, const std::optional<RemotePayload> &pending) {
  Json::Value root(Json::objectValue);
  root["schema_version"] = 1;
  root["last_result"] = result;
  if (result != "check_failed")
    root["last_successful_check_utc"] = NowUtcString();
  if (pending)
    root["pending_update"] = RemotePayloadToJson(*pending);
  std::string error;
  JsonWriteFile(state_path, root, &error);
}

int RunBootstrapFlow(const BootstrapOptions &options) {
  BootstrapTrace("RunBootstrapFlow start role=" + std::string(RoleToCString(options.role)) +
                 " worker_mode=" + std::to_string(options.worker_mode ? 1 : 0) +
                 " approved_install=" + std::to_string(options.approved_install ? 1 : 0) +
                 " skip_update_check=" + std::to_string(options.skip_update_check ? 1 : 0) +
                 " quiet_status=" + std::to_string(options.quiet_status ? 1 : 0) +
                 " install_root=" + GenericPath(options.install_root));
  BootstrapTrace("RunBootstrapFlow create_ui");
  UiHandle ui(options.role, options.quiet_status);
  BootstrapTrace("RunBootstrapFlow ui_ready");
  ui->SetStatus("Preparing WORR", "Starting bootstrap updater.");
  BootstrapTrace("RunBootstrapFlow initial_status_set");

  if (options.skip_update_check) {
    BootstrapTrace("RunBootstrapFlow skip_update_check launch");
    ui->SetStatus("Launching WORR", "Starting the installed build without another update check.");
    return LaunchEngineAndWait(options.install_root, options.launch_relpath, options.engine_library_relpath,
                               options.forwarded_args, &ui);
  }

  const fs::path config_path = options.install_root / kConfigName;
  const fs::path local_manifest_path = options.install_root / kLocalManifestName;
  const fs::path state_path = options.install_root / kStateName;
  BootstrapTrace("RunBootstrapFlow paths config=" + GenericPath(config_path) +
                 " manifest=" + GenericPath(local_manifest_path) + " state=" + GenericPath(state_path));

  if (!fs::is_regular_file(config_path) || !fs::is_regular_file(local_manifest_path)) {
    BootstrapTrace("RunBootstrapFlow dev_build config_or_manifest_missing");
    ui->SetStatus("Developer build detected", "Skipping update checks and launching the hosted engine directly.");
    return LaunchEngineAndWait(options.install_root, options.launch_relpath, options.engine_library_relpath,
                               options.forwarded_args, &ui);
  }

  BootstrapTrace("RunBootstrapFlow load_config");
  const UpdaterConfig config = LoadUpdaterConfig(config_path, options.role);
  BootstrapTrace("RunBootstrapFlow config_loaded repo=" + config.repo + " channel=" + config.channel +
                 " role=" + config.role + " autolaunch=" + std::to_string(config.autolaunch ? 1 : 0));
  BootstrapTrace("RunBootstrapFlow load_local_manifest");
  const InstallManifest local_manifest = LoadInstallManifest(local_manifest_path);
  BootstrapTrace("RunBootstrapFlow local_manifest_loaded version=" + local_manifest.version +
                 " role=" + local_manifest.role);

  std::optional<RemotePayload> available_update;
  std::string warning;

  try {
    const auto deadline = clock_type::now() + std::chrono::milliseconds(kDiscoveryBudgetMs);
    RemotePayload discovered = DiscoverRemotePayload(config, options.role, local_manifest, &*ui, deadline);
    if (!discovered.version.empty() && CompareSemver(local_manifest.version, discovered.version) < 0) {
      available_update = discovered;
      BootstrapTrace("RunBootstrapFlow update_available version=" + discovered.version);
      SaveState(state_path, "update_available", available_update);
    } else {
      BootstrapTrace("RunBootstrapFlow current version=" + local_manifest.version);
      SaveState(state_path, "current", std::nullopt);
    }
  } catch (const std::exception &e) {
    bool pending_found = false;
    RemotePayload pending = LoadPendingUpdate(state_path, &pending_found);
    if (pending_found)
      available_update = pending;
    else
      SaveState(state_path, "check_failed", std::nullopt);
    BootstrapTrace(std::string("RunBootstrapFlow discovery_warning pending=") +
                   std::to_string(pending_found ? 1 : 0) + " error=" + e.what());
    warning = e.what();
  }

  if (available_update) {
    const std::string detail =
        warning.empty() ? ("Version " + available_update->version + " is ready to install.")
                        : ("A newer build is already known locally. " + warning);
    const bool install = options.approved_install || ui->PromptInstall("Update required", detail);
    if (!install) {
      BootstrapTrace("RunBootstrapFlow update_deferred");
      ui->SetStatus("Update deferred", "Launching the installed build without applying the update.");
    } else {
      if (!options.worker_mode) {
        BootstrapTrace("RunBootstrapFlow launch_temp_worker");
        ui->SetStatus("Restarting updater", "Launching a temporary updater worker to apply the approved install.");
        return LaunchApprovedUpdateWorker(options);
      }

      BootstrapTrace("RunBootstrapFlow worker_download_start");
      ui->SetStatus("Downloading update", "Fetching the approved update package.");
      const fs::path temp_root = fs::temp_directory_path() / ("worr-bootstrap-" + RandomToken());
      const fs::path archive_path = temp_root / available_update->update_package_name;
      const fs::path stage_dir = temp_root / "stage";
      fs::create_directories(stage_dir);

      const int package_timeout = 30 * 60 * 1000;
      HttpDownloadFile(available_update->package_url, archive_path, package_timeout, &*ui,
                       "Downloading update package");

      const Json::Value package = available_update->manifest_json["package"];
      const std::string package_hash = JsonString(package, "sha256");
      if (!package_hash.empty() && ToLower(Sha256File(archive_path)) != ToLower(package_hash))
        throw std::runtime_error("Downloaded package hash mismatch");

      BootstrapTrace("RunBootstrapFlow worker_extract_start root=" + GenericPath(temp_root));
      ExtractZip(archive_path, stage_dir, &*ui);
#if defined(_WIN32)
      bool applied = false;
      for (int attempt = 0; attempt < kApplyRetryCount; ++attempt) {
        try {
          BootstrapTrace("RunBootstrapFlow apply_attempt=" + std::to_string(attempt + 1));
          ApplyPayload(stage_dir, options.install_root, config, *available_update, &*ui);
          applied = true;
          BootstrapTrace("RunBootstrapFlow apply_success");
          break;
        } catch (const fs::filesystem_error &e) {
          BootstrapTrace(std::string("RunBootstrapFlow apply_filesystem_error code=") +
                         std::to_string(e.code().value()) + " message=" + e.what());
          if (!IsPermissionDeniedError(e.code()))
            throw;
          if (attempt + 1 < kApplyRetryCount) {
            ui->SetStatus("Waiting for launcher shutdown",
                          "Finishing the previous process handoff before applying the update.");
            SDL_Delay(kApplyRetryDelayMs);
            continue;
          }
          if (!IsProcessElevated()) {
            BootstrapTrace("RunBootstrapFlow elevation_requested");
            ui->SetStatus("Waiting for administrator approval", "WORR needs elevation to update this install.");
            std::vector<std::string> args = BuildWorkerInvocation(options, true);
            if (!RelaunchElevated(options.worker_exe_path, args, options.install_root))
              throw std::runtime_error("Failed to request elevated updater access");
            return 0;
          }
          throw;
        }
      }
      if (!applied)
        throw std::runtime_error("Failed applying update payload");
#else
      ApplyPayload(stage_dir, options.install_root, config, *available_update, &*ui);
#endif
      BootstrapTrace("RunBootstrapFlow save_state_current");
      SaveState(state_path, "current", std::nullopt);
      std::error_code ignored;
      fs::remove_all(temp_root, ignored);

      if (!config.autolaunch) {
        BootstrapTrace("RunBootstrapFlow update_installed_no_autolaunch");
        ui->SetStatus("Update installed", "WORR was updated successfully. Launch it again to start the new build.");
        return 0;
      }

      BootstrapTrace("RunBootstrapFlow relaunch_updated_bootstrap");
      ui->SetStatus("Update installed", "Restarting the public WORR bootstrap with the updated build.");
      return RelaunchInstalledBootstrap(options);
    }
  } else if (!warning.empty()) {
    BootstrapTrace("RunBootstrapFlow warning_launch_installed error=" + warning);
    ui->SetStatus("Update check unavailable", warning + " Launching the installed build.");
  } else {
    BootstrapTrace("RunBootstrapFlow no_update_required");
    ui->SetStatus("WORR is current", "No update was required.");
  }

  BootstrapTrace("RunBootstrapFlow launch_installed_engine");
  return LaunchEngineAndWait(options.install_root, options.launch_relpath, options.engine_library_relpath,
                             options.forwarded_args, &ui);
}

BootstrapOptions ParseBootstrapOptions(int argc, char **argv) {
  BootstrapOptions options;
  options.worker_mode = true;
  if (argc > 0)
    options.worker_exe_path = fs::absolute(Utf8Path(argv[0]));
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--bootstrap") {
      continue;
    }
    if (arg == "--role" && i + 1 < argc) {
      const auto role = ParseRole(argv[++i]);
      if (!role)
        throw std::runtime_error("Invalid role");
      options.role = *role;
      continue;
    }
    if (arg == "--install-root" && i + 1 < argc) {
      options.install_root = NormalizeInstallRoot(Utf8Path(argv[++i]));
      continue;
    }
    if (arg == "--launch-rel" && i + 1 < argc) {
      options.launch_relpath = argv[++i];
      continue;
    }
    if ((arg == "--engine-rel" || arg == "--runtime-rel") && i + 1 < argc) {
      options.engine_library_relpath = argv[++i];
      continue;
    }
    if (arg == "--approved-install") {
      options.approved_install = true;
      continue;
    }
    if (arg == kQuietStatusArg) {
      options.quiet_status = true;
      continue;
    }
    if (arg == "--") {
      for (int j = i + 1; j < argc; ++j)
        options.forwarded_args.push_back(argv[j]);
      break;
    }
  }
  if (options.install_root.empty())
    options.install_root = BasePathFromExecutable();
  if (options.launch_relpath.empty())
    options.launch_relpath = DefaultLaunchRelpath(options.role);
  if (options.engine_library_relpath.empty())
    options.engine_library_relpath = DefaultEngineLibraryRelpath(options.role);
  BootstrapTrace("ParseBootstrapOptions role=" + std::string(RoleToCString(options.role)) +
                 " approved_install=" + std::to_string(options.approved_install ? 1 : 0) +
                 " skip_update_check=" + std::to_string(options.skip_update_check ? 1 : 0) +
                 " quiet_status=" + std::to_string(options.quiet_status ? 1 : 0) +
                 " forwarded_argc=" + std::to_string(options.forwarded_args.size()));
  return options;
}

} // namespace

const char *RoleToCString(Role role) { return role == Role::Client ? "client" : "server"; }

int RunLauncher(Role role, const std::string &launch_relative_path, const std::string &engine_library_relative_path,
                int argc, char **argv) {
  try {
    const fs::path install_root = BasePathFromExecutable();

    BootstrapOptions options;
    options.role = role;
    options.install_root = NormalizeInstallRoot(install_root);
    options.worker_exe_path = install_root / kUpdaterStem;
    options.launch_relpath = launch_relative_path;
    options.engine_library_relpath = engine_library_relative_path;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == kSkipUpdateCheckArg) {
        options.skip_update_check = true;
        continue;
      }
      if (arg == kQuietStatusArg) {
        options.quiet_status = true;
        continue;
      }
      options.forwarded_args.push_back(argv[i]);
    }
    return RunBootstrapFlow(options);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "WORR launcher error: %s\n", e.what());
    return 1;
  }
}

int RunWorker(int argc, char **argv) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  int result = 0;
  try {
    BootstrapOptions options = ParseBootstrapOptions(argc, argv);
#if defined(_WIN32)
    if (options.role == Role::Server)
      AttachToParentConsole();
#endif
    result = RunBootstrapFlow(options);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "WORR updater error: %s\n", e.what());
    result = 1;
  }
  curl_global_cleanup();
  return result;
}

#if defined(_WIN32)
int RunLauncherWide(Role role, const std::string &launch_relative_path,
                    const std::string &engine_library_relative_path, int argc, wchar_t **argv) {
  std::vector<std::string> utf8_args;
  utf8_args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i)
    utf8_args.push_back(WideToUtf8(argv[i]));

  std::vector<char *> raw_args;
  raw_args.reserve(utf8_args.size() + 1);
  for (std::string &arg : utf8_args)
    raw_args.push_back(arg.data());
  raw_args.push_back(nullptr);

  return RunLauncher(role, launch_relative_path, engine_library_relative_path, argc, raw_args.data());
}

int RunWorkerWide(int argc, wchar_t **argv) {
  std::vector<std::string> utf8_args;
  utf8_args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i)
    utf8_args.push_back(WideToUtf8(argv[i]));

  std::vector<char *> raw_args;
  raw_args.reserve(utf8_args.size() + 1);
  for (std::string &arg : utf8_args)
    raw_args.push_back(arg.data());
  raw_args.push_back(nullptr);

  return RunWorker(argc, raw_args.data());
}
#endif

} // namespace worr::updater
