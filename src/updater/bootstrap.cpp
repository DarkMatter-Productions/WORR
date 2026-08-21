#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "archive_path.h"
#include "bootstrap_shared.h"
#include "common/bootstrap.h"
#include "install_root.h"
#include "install_transaction.h"
#include "release_signature.h"
#include "sha256.h"
#include "zip_attributes.h"

#include <SDL3/SDL.h>
#if USE_SDL3_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif
#include <curl/curl.h>
#include <json/json.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
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

#include "bootstrap_logo_rgba.h"
#include "miniz.h"

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
#else
#include <unistd.h>
#endif

namespace worr::updater {

namespace fs = std::filesystem;
using clock_type = std::chrono::steady_clock;

namespace {

constexpr const char *kConfigName = "worr_update.json";
constexpr const char *kStateName = "worr_update_state.json";
constexpr const char *kLocalManifestName = "worr_install_manifest.json";
constexpr const char *kRuntimeCoordinationLockName = "worr_install.lock";
constexpr const char *kEmptySha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr const char *kElevatedWorkerPath = ".worr-update/elevated-worker.exe";
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
constexpr const char *kBootstrapWin32HwndEnv = "WORR_BOOTSTRAP_WIN32_HWND";
constexpr const char *kBootstrapTransitionEnv = "WORR_BOOTSTRAP_TRANSITION";
constexpr const char *kEngineEntryPoint = "WORR_EngineMain";
constexpr const char *kReadyCallbackEntryPoint =
    "Com_SetBootstrapReadyCallback";
constexpr const char *kSkipUpdateCheckArg = "--bootstrap-skip-update-check";
constexpr const char *kQuietStatusArg = "--bootstrap-quiet-status";
constexpr int kConnectTimeoutMs = 2000;
constexpr int kDiscoveryBudgetMs = 15000;
constexpr int kMaxReleaseFeedPages = 3;
constexpr int kReleaseFeedPageSize = 100;
constexpr std::time_t kMaximumReleaseIndexLifetimeSeconds = 366 * 24 * 60 * 60;
constexpr std::time_t kReleaseClockSkewSeconds = 24 * 60 * 60;
constexpr size_t kMaxRevokedReleaseIndices = 1024;
constexpr int kApplyRetryCount = 20;
constexpr int kApplyRetryDelayMs = 100;
constexpr size_t kMaxUpdaterConfigBytes = 1024u * 1024u;
constexpr size_t kMaxHttpMetadataBytes = 32u * 1024u * 1024u;
constexpr size_t kMaxSignatureEnvelopeBytes = 1024u * 1024u;
constexpr size_t kMaxLocalManifestBytes = 160u * 1024u * 1024u;
constexpr uintmax_t kMaxPendingStateBytes = 160u * 1024u * 1024u;
constexpr uint64_t kMaxUpdatePackageBytes = uint64_t{4} * 1024u * 1024u * 1024u;
constexpr uint64_t kMaxUpdateFileBytes = uint64_t{4} * 1024u * 1024u * 1024u;
constexpr uint64_t kMaxExpandedUpdateBytes =
    uint64_t{16} * 1024u * 1024u * 1024u;
// One additional journal entry is reserved for the local install manifest.
constexpr size_t kMaxManagedFiles = 99999;
constexpr uint64_t kUiTickDelayMs = 16;
constexpr uint64_t kMinimumSplashDisplayMs = 750;
constexpr const char *kUserAgent = "WORR-Bootstrap/1.0";

struct SemverIdentifier {
  bool numeric = false;
  uint64_t number = 0;
  std::string text;
};

struct SemverVersion {
  bool valid = false;
  uint64_t major = 0;
  uint64_t minor = 0;
  uint64_t patch = 0;
  std::vector<SemverIdentifier> prerelease;
};

struct FileEntry {
  std::string path;
  std::string sha256;
  uint64_t size = 0;
  uint32_t mode = 0;
};

struct UpdaterConfig {
  std::string repo = "themuffinator/WORR";
  std::string channel = "stable";
  std::string role;
  std::string release_index_asset = "worr-release-index-stable.json";
  std::string launch_exe;
  bool autolaunch = true;
  bool allow_prerelease = false;
  bool allow_unsigned_development_index = false;
  std::optional<ReleaseTrustPolicy> release_trust_policy;
  std::vector<std::string> preserve = {
      "worr_update.json",    "worr_update_state.json", "basew/*.cfg",
      "basew/autoexec.cfg",  "basew/config.cfg",       "basew/save/*",
      "basew/saves/*",       "basew/screenshots/*",    "basew/demos/*",
      "basew/logs/*",
  };
};

struct AssetIntegrity {
  std::string sha256;
  uint64_t size = 0;
};

struct InstallManifest {
  std::string version;
  std::string role;
  std::string launch_exe;
  std::string engine_library;
  std::string local_manifest_name;
  std::vector<FileEntry> files;
  bool authenticated_files = false;
  uint64_t authenticated_release_sequence = 0;
  std::string authenticated_release_index_sha256;
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
  std::string release_index_name;
  std::string release_index_sha256;
  std::string raw_release_index;
  std::string raw_release_signature;
  std::string raw_update_manifest;
  std::vector<std::string> verified_release_key_ids;
  std::vector<std::string> revoked_release_index_sha256s;
  std::string release_expires_at_utc;
  uint64_t release_sequence = 0;
  bool unsigned_development_index = false;
  AssetIntegrity update_manifest_integrity;
  AssetIntegrity update_package_integrity;
  Json::Value manifest_json;
};

enum class InstallActionKind {
  Add,
  Refresh,
};

struct InstallAction {
  InstallActionKind kind = InstallActionKind::Add;
  FileEntry file;
};

struct InstallSyncPlan {
  std::vector<InstallAction> install_actions;
  std::vector<std::string> remove_paths;
  uint64_t unchanged_count = 0;
  bool metadata_change = false;
  bool version_change = false;

  bool RequiresSync() const {
    return metadata_change || !install_actions.empty() || !remove_paths.empty();
  }
  bool RequiresPackagePayload() const { return !install_actions.empty(); }
  uint64_t TotalApplySteps() const {
    return static_cast<uint64_t>(install_actions.size()) +
           static_cast<uint64_t>(remove_paths.size());
  }

  uint64_t AddCount() const {
    return static_cast<uint64_t>(
        std::count_if(install_actions.begin(), install_actions.end(),
                      [](const InstallAction &a) {
                        return a.kind == InstallActionKind::Add;
                      }));
  }

  uint64_t RefreshCount() const {
    return static_cast<uint64_t>(
        std::count_if(install_actions.begin(), install_actions.end(),
                      [](const InstallAction &a) {
                        return a.kind == InstallActionKind::Refresh;
                      }));
  }
};

enum class SessionShellWindowMode {
  Windowed,
  BorderlessWindow,
  BorderlessFullscreen,
  ExclusiveFullscreen,
  SpanBorderless,
};

struct SessionShellWindowConfig {
  fs::path runtime_root;
  SessionShellWindowMode mode = SessionShellWindowMode::Windowed;
  int width = 640;
  int height = 480;
  int x = SDL_WINDOWPOS_CENTERED;
  int y = SDL_WINDOWPOS_CENTERED;
  bool geometry_specified = false;
  int monitor_mode = 0;
  int fullscreen_index = 0;
  int borderless = 1;
  bool fullscreen_exclusive = true;
  bool fullscreen_capture_friendly = true;
  std::string display_request = "0";
};

struct SessionShellWindowPlacement {
  SDL_DisplayID display_id = 0;
  SDL_Rect bounds{0, 0, 0, 0};
  bool has_bounds = false;
  bool invalid_display = false;
  int width = 640;
  int height = 480;
  int x = SDL_WINDOWPOS_CENTERED;
  int y = SDL_WINDOWPOS_CENTERED;
};

struct BootstrapOptions {
  Role role = Role::Client;
  fs::path install_root;
  fs::path worker_exe_path;
  std::string launch_relpath;
  std::string engine_library_relpath;
  std::vector<std::string> forwarded_args;
  std::string approved_config_sha256;
  std::string approved_release_index_sha256;
  uint64_t approved_release_sequence = 0;
  uint32_t wait_for_process_id = 0;
  uint64_t wait_for_process_creation_time = 0;
  bool approved_install = false;
  bool stage_elevated_worker = false;
  bool worker_mode = false;
  bool recover_only = false;
  bool skip_update_check = false;
  bool quiet_status = false;
};

Json::Value ParseJsonString(const std::string &text,
                            const std::string &context);
std::string CurrentPlatformId();
void RevalidateCachedRemotePayload(RemotePayload *payload,
                                   const UpdaterConfig &config,
                                   const BootstrapOptions &options,
                                   bool require_current_freshness = true);
Json::Value BuildLocalInstallManifest(const RemotePayload &payload);

using engine_main_fn = int (*)(int argc, char **argv);
using set_ready_callback_fn = void (*)(com_bootstrap_ready_callback_t callback,
                                       void *userdata);

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
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

// A launcher hosts the engine in-process, so its own UI must obey the same
// no-window contract as the engine it is about to start. Keep this parser
// deliberately limited to the command-line cvar form understood by the engine;
// later +set occurrences retain their normal override semantics.
bool ForwardedArgsRequestHeadless(const std::vector<std::string> &args) {
  bool headless = false;
  for (size_t i = 0; i + 2 < args.size(); ++i) {
    if (ToLower(args[i]) != "+set" || ToLower(args[i + 1]) != "win_headless")
      continue;

    const std::string value = Trim(args[i + 2]);
    char *end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    headless = end != value.c_str() && *end == '\0' && parsed != 0;
    i += 2;
  }
  return headless;
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
#if defined(_WIN32)
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD elevation_size = sizeof(elevation);
    const bool privileged =
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
        GetTokenInformation(token, TokenElevation, &elevation,
                            sizeof(elevation), &elevation_size) &&
        elevation.TokenIsElevated != 0;
    if (token)
      CloseHandle(token);
#else
    const bool privileged = geteuid() == 0;
#endif
    const char *value = std::getenv("WORR_BOOTSTRAP_TRACE");
    // A caller controls both the inherited environment and the shared temp
    // namespace. Never let a privileged updater follow a caller-planted trace
    // path before its protected install root has been authenticated.
    enabled = !privileged && value && *value && std::string(value) != "0" ? 1 : 0;
  }
  return enabled != 0;
}

void BootstrapTrace(const std::string &message) {
  if (!BootstrapTraceEnabled())
    return;

  try {
    const fs::path path =
        fs::temp_directory_path() / "worr-bootstrap-trace.log";
    std::ofstream file(path, std::ios::app);
    if (!file.is_open())
      return;
    std::string single_line = message;
    std::replace_if(single_line.begin(), single_line.end(),
                    [](char value) { return value == '\r' || value == '\n'; },
                    ' ');
    file << NowUtcString() << " " << single_line << '\n';
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

std::string PointerString(const void *pointer) {
  std::ostringstream ss;
  ss << std::hex << reinterpret_cast<uintptr_t>(pointer);
  return ss.str();
}

fs::path Utf8Path(const std::string &path) { return fs::u8path(path); }

std::string GenericPath(const fs::path &path) {
  return path.generic_u8string();
}

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

std::string RequireSafeUpdaterPath(const std::string &path,
                                   std::string_view description,
                                   bool allow_trailing_separator = false) {
  if (!IsSafeUpdaterRelativePath(path, allow_trailing_separator))
    throw std::runtime_error("Unsafe " + std::string(description) + ": " +
                             path);
  return CanonicalUpdaterRelativePath(path);
}

std::string RequireSafeUpdaterFileName(const std::string &path,
                                       std::string_view description) {
  std::string normalized = RequireSafeUpdaterPath(path, description);
  if (normalized.find('/') != std::string::npos)
    throw std::runtime_error("Unsafe " + std::string(description) + ": " +
                             path);
  return normalized;
}

fs::path NormalizeInstallRoot(fs::path path) {
  std::string text = path.lexically_normal().generic_u8string();
  if (text.size() > 2 && text.compare(text.size() - 2, 2, "/.") == 0)
    text.resize(text.size() - 2);
  while (text.size() > 1 && (text.back() == '/' || text.back() == '\\')) {
#if defined(_WIN32)
    if (text.size() == 3 && std::isalpha(static_cast<unsigned char>(text[0])) &&
        text[1] == ':')
      break;
#endif
    text.pop_back();
  }
  return Utf8Path(text);
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

bool ParseIntStrict(const std::string &text, int *out) {
  if (!out || text.empty())
    return false;

  char *end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (!end || *end != '\0')
    return false;
  if (value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max())
    return false;

  *out = static_cast<int>(value);
  return true;
}

std::string StripMatchingQuotes(std::string text) {
  text = Trim(std::move(text));
  if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                           (text.front() == '\'' && text.back() == '\''))) {
    text = text.substr(1, text.size() - 2);
  }
  return text;
}

bool ParseGeometryString(const std::string &text, int *x, int *y, int *width,
                         int *height) {
  if (!x || !y || !width || !height)
    return false;

  const char *cursor = text.c_str();
  if (!*cursor)
    return false;

  char *end = nullptr;
  const unsigned long parsed_width = std::strtoul(cursor, &end, 10);
  if (!end || (*end != 'x' && *end != 'X'))
    return false;

  cursor = end + 1;
  const unsigned long parsed_height = std::strtoul(cursor, &end, 10);
  if (!end)
    return false;

  long parsed_x = 0;
  long parsed_y = 0;
  if (*end == '+' || *end == '-') {
    parsed_x = std::strtol(end, &end, 10);
    if (*end == '+' || *end == '-') {
      parsed_y = std::strtol(end, &end, 10);
    }
  }

  if (*end != '\0')
    return false;
  if (parsed_width < 1 || parsed_width > 8192 || parsed_height < 1 ||
      parsed_height > 8192)
    return false;
  if (parsed_x < std::numeric_limits<int>::min() ||
      parsed_x > std::numeric_limits<int>::max() ||
      parsed_y < std::numeric_limits<int>::min() ||
      parsed_y > std::numeric_limits<int>::max())
    return false;

  *x = static_cast<int>(parsed_x);
  *y = static_cast<int>(parsed_y);
  *width = static_cast<int>(parsed_width);
  *height = static_cast<int>(parsed_height);
  return true;
}

bool IsSetCommand(std::string_view token) {
  return EqualsIgnoreCase(token, "+set") || EqualsIgnoreCase(token, "+seta") ||
         EqualsIgnoreCase(token, "+sets") || EqualsIgnoreCase(token, "+setu") ||
         EqualsIgnoreCase(token, "+setr");
}

void OverlayCvarsFromCommandLine(const std::vector<std::string> &args,
                                 std::map<std::string, std::string> *cvars) {
  if (!cvars)
    return;

  for (size_t i = 0; i < args.size(); ++i) {
    if (!IsSetCommand(args[i]) || i + 2 >= args.size())
      continue;
    (*cvars)[ToLower(args[i + 1])] = args[i + 2];
    i += 2;
  }
}

void OverlayCvarsFromFile(const fs::path &path,
                          std::map<std::string, std::string> *cvars) {
  if (!cvars || !fs::is_regular_file(path))
    return;

  std::ifstream stream(path);
  if (!stream)
    return;

  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed.rfind("//", 0) == 0 || trimmed[0] == '#')
      continue;

    size_t cursor = 0;
    auto next_token = [&](std::string *token) -> bool {
      while (cursor < trimmed.size() &&
             std::isspace(static_cast<unsigned char>(trimmed[cursor])))
        ++cursor;
      if (cursor >= trimmed.size())
        return false;
      const size_t start = cursor;
      while (cursor < trimmed.size() &&
             !std::isspace(static_cast<unsigned char>(trimmed[cursor])))
        ++cursor;
      *token = trimmed.substr(start, cursor - start);
      return true;
    };

    std::string command;
    std::string name;
    if (!next_token(&command) || !next_token(&name))
      continue;

    const std::string lowered = ToLower(command);
    if (lowered != "set" && lowered != "seta" && lowered != "sets" &&
        lowered != "setu" && lowered != "setr")
      continue;

    while (cursor < trimmed.size() &&
           std::isspace(static_cast<unsigned char>(trimmed[cursor])))
      ++cursor;

    (*cvars)[ToLower(name)] = StripMatchingQuotes(trimmed.substr(cursor));
  }
}

std::optional<int> ParseDisplayOrdinalHint(const std::string &text) {
  if (text.empty())
    return std::nullopt;

  int value = 0;
  if (ParseIntStrict(text, &value))
    return value;

  const std::string lowered = ToLower(text);
  const size_t pos = lowered.rfind("display");
  if (pos == std::string::npos)
    return std::nullopt;

  const std::string suffix = lowered.substr(pos + 7);
  if (suffix.empty())
    return std::nullopt;
  if (!std::all_of(suffix.begin(), suffix.end(),
                   [](unsigned char c) { return std::isdigit(c); }))
    return std::nullopt;

  if (!ParseIntStrict(suffix, &value))
    return std::nullopt;
  return value;
}

const char *SessionShellWindowModeToCString(SessionShellWindowMode mode) {
  switch (mode) {
  case SessionShellWindowMode::Windowed:
    return "windowed";
  case SessionShellWindowMode::BorderlessWindow:
    return "borderless_window";
  case SessionShellWindowMode::BorderlessFullscreen:
    return "borderless_fullscreen";
  case SessionShellWindowMode::ExclusiveFullscreen:
    return "exclusive_fullscreen";
  case SessionShellWindowMode::SpanBorderless:
    return "span_borderless";
  }
  return "windowed";
}

fs::path
ResolveClientRuntimeRoot(const fs::path &install_root,
                         const std::map<std::string, std::string> &cli_cvars) {
  const auto it = cli_cvars.find("basedir");
  if (it == cli_cvars.end() || it->second.empty())
    return install_root;

  fs::path runtime_root = Utf8Path(it->second);
  if (!runtime_root.is_absolute())
    runtime_root = install_root / runtime_root;
  return NormalizeInstallRoot(runtime_root);
}

SessionShellWindowConfig LoadClientSessionShellWindowConfig(
    const fs::path &install_root,
    const std::vector<std::string> &forwarded_args) {
  SessionShellWindowConfig config;
  std::map<std::string, std::string> cvars;
  OverlayCvarsFromCommandLine(forwarded_args, &cvars);

  config.runtime_root = ResolveClientRuntimeRoot(install_root, cvars);
  OverlayCvarsFromFile(config.runtime_root / "basew" / "config.cfg", &cvars);
  OverlayCvarsFromFile(config.runtime_root / "basew" / "autoexec.cfg", &cvars);
  OverlayCvarsFromCommandLine(forwarded_args, &cvars);

  const auto geometry_it = cvars.find("r_geometry");
  if (geometry_it != cvars.end()) {
    int parsed_x = 0;
    int parsed_y = 0;
    int parsed_width = 0;
    int parsed_height = 0;
    if (ParseGeometryString(geometry_it->second, &parsed_x, &parsed_y,
                            &parsed_width, &parsed_height)) {
      config.x = parsed_x;
      config.y = parsed_y;
      config.width = parsed_width;
      config.height = parsed_height;
      config.geometry_specified = true;
    }
  }

  if (const auto it = cvars.find("r_monitor_mode"); it != cvars.end()) {
    int value = 0;
    if (ParseIntStrict(it->second, &value))
      config.monitor_mode = std::clamp(value, 0, 2);
  }
  if (const auto it = cvars.find("r_fullscreen"); it != cvars.end()) {
    int value = 0;
    if (ParseIntStrict(it->second, &value))
      config.fullscreen_index = std::max(0, value);
  }
  if (const auto it = cvars.find("r_borderless"); it != cvars.end()) {
    int value = 1;
    if (ParseIntStrict(it->second, &value))
      config.borderless = std::clamp(value, 0, 2);
  }
  if (const auto it = cvars.find("r_fullscreen_exclusive"); it != cvars.end()) {
    int value = 1;
    if (ParseIntStrict(it->second, &value))
      config.fullscreen_exclusive = value != 0;
  }
  if (const auto it = cvars.find("win_fullscreen_capture_friendly");
      it != cvars.end()) {
    int value = 1;
    if (ParseIntStrict(it->second, &value))
      config.fullscreen_capture_friendly = value != 0;
  }
  if (const auto it = cvars.find("r_display");
      it != cvars.end() && !it->second.empty())
    config.display_request = it->second;

  if (config.fullscreen_index > 0) {
    if (config.monitor_mode == 2) {
      config.mode = SessionShellWindowMode::SpanBorderless;
    } else if (config.borderless >= 1) {
      config.mode = SessionShellWindowMode::BorderlessFullscreen;
    } else {
      config.mode = SessionShellWindowMode::ExclusiveFullscreen;
    }
  } else if (config.borderless == 2) {
    config.mode = SessionShellWindowMode::BorderlessWindow;
  }

  std::ostringstream geometry_detail;
  geometry_detail << config.width << 'x' << config.height;
  if (config.geometry_specified)
    geometry_detail << std::showpos << config.x << config.y << std::noshowpos;

  BootstrapTrace(
      "LoadClientSessionShellWindowConfig root=" +
      GenericPath(config.runtime_root) +
      " mode=" + SessionShellWindowModeToCString(config.mode) +
      " fullscreen_index=" + std::to_string(config.fullscreen_index) +
      " borderless=" + std::to_string(config.borderless) +
      " exclusive=" + std::to_string(config.fullscreen_exclusive ? 1 : 0) +
      " capture_friendly=" +
      std::to_string(config.fullscreen_capture_friendly ? 1 : 0) +
      " monitor_mode=" + std::to_string(config.monitor_mode) + " display=\"" +
      config.display_request + "\" geometry=" + geometry_detail.str());
  return config;
}

SDL_DisplayID
ResolveSessionShellDisplayId(const SessionShellWindowConfig &config,
                             bool *invalid) {
  if (invalid)
    *invalid = false;

  if (config.monitor_mode != 1 || config.display_request.empty() ||
      config.display_request == "0")
    return SDL_GetPrimaryDisplay();

  int num_displays = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&num_displays);
  if (!displays || num_displays < 1) {
    if (invalid)
      *invalid = true;
    SDL_free(displays);
    return SDL_GetPrimaryDisplay();
  }

  SDL_DisplayID resolved = 0;
  if (const std::optional<int> ordinal =
          ParseDisplayOrdinalHint(config.display_request);
      ordinal.has_value()) {
    if (*ordinal == 0) {
      resolved = SDL_GetPrimaryDisplay();
    } else if (*ordinal > 0 && *ordinal <= num_displays) {
      resolved = displays[*ordinal - 1];
    } else if (invalid) {
      *invalid = true;
    }
  } else {
    for (int i = 0; i < num_displays; ++i) {
      const char *name = SDL_GetDisplayName(displays[i]);
      if (name && EqualsIgnoreCase(name, config.display_request)) {
        resolved = displays[i];
        break;
      }
    }
    if (!resolved && invalid)
      *invalid = true;
  }

  SDL_free(displays);
  return resolved ? resolved : SDL_GetPrimaryDisplay();
}

bool ResolveSessionShellSpanBounds(SDL_Rect *bounds) {
  if (!bounds)
    return false;

  int num_displays = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&num_displays);
  if (!displays || num_displays < 1) {
    SDL_free(displays);
    return false;
  }

  SDL_Rect rect{};
  bool have_bounds = false;
  for (int i = 0; i < num_displays; ++i) {
    if (!SDL_GetDisplayBounds(displays[i], &rect))
      continue;

    if (!have_bounds) {
      *bounds = rect;
      have_bounds = true;
      continue;
    }

    const int right = std::max(bounds->x + bounds->w, rect.x + rect.w);
    const int bottom = std::max(bounds->y + bounds->h, rect.y + rect.h);
    bounds->x = std::min(bounds->x, rect.x);
    bounds->y = std::min(bounds->y, rect.y);
    bounds->w = right - bounds->x;
    bounds->h = bottom - bounds->y;
  }

  SDL_free(displays);
  return have_bounds;
}

SessionShellWindowPlacement
ResolveSessionShellPlacement(const SessionShellWindowConfig &config) {
  SessionShellWindowPlacement placement;
  placement.width = config.width;
  placement.height = config.height;
  placement.x = config.x;
  placement.y = config.y;
  placement.display_id =
      ResolveSessionShellDisplayId(config, &placement.invalid_display);

  if (config.mode == SessionShellWindowMode::SpanBorderless) {
    placement.has_bounds = ResolveSessionShellSpanBounds(&placement.bounds);
  } else if (placement.display_id) {
    placement.has_bounds =
        SDL_GetDisplayBounds(placement.display_id, &placement.bounds);
  }

  const bool fullscreen =
      config.mode == SessionShellWindowMode::BorderlessFullscreen ||
      config.mode == SessionShellWindowMode::ExclusiveFullscreen ||
      config.mode == SessionShellWindowMode::SpanBorderless;
  if (fullscreen && placement.has_bounds) {
    placement.width = placement.bounds.w;
    placement.height = placement.bounds.h;
    placement.x = placement.bounds.x;
    placement.y = placement.bounds.y;
  } else if (!config.geometry_specified) {
    placement.x = SDL_WINDOWPOS_CENTERED;
    placement.y = SDL_WINDOWPOS_CENTERED;
  }

  return placement;
}

std::optional<SDL_DisplayMode>
ResolveExclusiveDisplayMode(SDL_DisplayID display_id, int fullscreen_index) {
  if (!display_id)
    return std::nullopt;

  if (fullscreen_index <= 1) {
    const SDL_DisplayMode *desktop_mode = SDL_GetDesktopDisplayMode(display_id);
    if (!desktop_mode)
      return std::nullopt;
    return *desktop_mode;
  }

  int num_modes = 0;
  SDL_DisplayMode **modes =
      SDL_GetFullscreenDisplayModes(display_id, &num_modes);
  if (!modes || num_modes < 1) {
    SDL_free(modes);
    return std::nullopt;
  }

  const int mode_index = fullscreen_index - 2;
  std::optional<SDL_DisplayMode> resolved;
  if (mode_index >= 0 && mode_index < num_modes && modes[mode_index]) {
    resolved = *modes[mode_index];
  }

  SDL_free(modes);
  return resolved;
}

std::string DefaultLaunchRelpath(Role role) {
  return role == Role::Client ? kClientLaunchStem : kServerLaunchStem;
}

std::string DefaultEngineLibraryRelpath(Role role) {
  return role == Role::Client ? kClientEngineLibraryStem
                              : kServerEngineLibraryStem;
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
    const char lhs =
        static_cast<char>(std::tolower(static_cast<unsigned char>(token)));
    const char rhs = static_cast<char>(
        std::tolower(static_cast<unsigned char>(value.front())));
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
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  *out = value;
  return true;
}

bool ParseSemver(const std::string &raw, SemverVersion *out) {
  *out = {};
  std::string value = Trim(raw);
  if (value.empty() || value.size() > 255)
    return false;
  if (value[0] == 'v' || value[0] == 'V')
    value.erase(value.begin());

  const auto valid_identifiers = [](std::string_view identifiers,
                                    bool reject_numeric_leading_zero) {
    if (identifiers.empty())
      return false;
    size_t start = 0;
    while (start <= identifiers.size()) {
      const size_t end = identifiers.find('.', start);
      const std::string_view identifier = identifiers.substr(
          start, end == std::string_view::npos ? identifiers.size() - start
                                                : end - start);
      if (identifier.empty() ||
          !std::all_of(identifier.begin(), identifier.end(), [](char value) {
            const unsigned char character = static_cast<unsigned char>(value);
            return std::isalnum(character) || value == '-';
          }))
        return false;
      const bool numeric = std::all_of(
          identifier.begin(), identifier.end(), [](char value) {
            return std::isdigit(static_cast<unsigned char>(value));
          });
      if (reject_numeric_leading_zero && numeric && identifier.size() > 1 &&
          identifier.front() == '0')
        return false;
      if (end == std::string_view::npos)
        return true;
      start = end + 1;
    }
    return false;
  };

  const size_t plus = value.find('+');
  if (plus != std::string::npos) {
    if (value.find('+', plus + 1) != std::string::npos ||
        !valid_identifiers(std::string_view(value).substr(plus + 1), false))
      return false;
    value.resize(plus);
  }

  std::string core = value;
  std::string prerelease_text;
  const size_t dash = value.find('-');
  if (dash != std::string::npos) {
    core = value.substr(0, dash);
    prerelease_text = value.substr(dash + 1);
    if (!valid_identifiers(prerelease_text, true))
      return false;
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
  const auto canonical_numeric = [](const std::string &part) {
    return !part.empty() && (part.size() == 1 || part.front() != '0');
  };
  if (!canonical_numeric(parts[0]) || !canonical_numeric(parts[1]) ||
      !canonical_numeric(parts[2]) || !ParseUnsigned(parts[0], &major) ||
      !ParseUnsigned(parts[1], &minor) || !ParseUnsigned(parts[2], &patch))
    return false;

  SemverVersion parsed;
  parsed.major = major;
  parsed.minor = minor;
  parsed.patch = patch;

  if (!prerelease_text.empty()) {
    std::stringstream ss(prerelease_text);
    std::string identifier;
    while (std::getline(ss, identifier, '.')) {
      if (identifier.empty())
        return false;
      uint64_t numeric = 0;
      SemverIdentifier item;
      const bool digits = std::all_of(
          identifier.begin(), identifier.end(), [](char value) {
            return std::isdigit(static_cast<unsigned char>(value));
          });
      if (digits) {
        if (!ParseUnsigned(identifier, &numeric))
          return false;
        item.numeric = true;
        item.number = numeric;
      } else {
        item.text = identifier;
      }
      parsed.prerelease.push_back(item);
    }
  }

  parsed.valid = true;
  *out = std::move(parsed);
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
  builder["allowComments"] = false;
  builder["allowTrailingCommas"] = false;
  builder["collectComments"] = false;
  builder["failIfExtra"] = true;
  builder["rejectDupKeys"] = true;
  builder["strictRoot"] = true;
  std::string errors;
  if (!Json::parseFromStream(builder, file, root, &errors)) {
    if (error)
      *error = errors;
    return false;
  }
  return true;
}

bool JsonWriteFile(const fs::path &path, const Json::Value &root,
                   std::string *error) {
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

std::string JsonString(const Json::Value &root, const char *key,
                       const std::string &fallback = {}) {
  if (root.isMember(key) && root[key].isString())
    return root[key].asString();
  return fallback;
}

std::string JsonStringAny(const Json::Value &root,
                          std::initializer_list<const char *> keys,
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

uint64_t JsonUInt64(const Json::Value &root, const char *key,
                    uint64_t fallback) {
  if (root.isMember(key) && root[key].isUInt64())
    return root[key].asUInt64();
  if (root.isMember(key) && root[key].isUInt())
    return root[key].asUInt();
  return fallback;
}

bool IsTrimmedPrintableAscii(std::string_view value) {
  return !value.empty() && value.size() <= 255 && value.front() != ' ' &&
         value.back() != ' ' &&
         std::all_of(value.begin(), value.end(), [](unsigned char byte) {
           return byte >= 0x20 && byte <= 0x7e;
         });
}

UpdaterConfig LoadUpdaterConfig(const std::string &contents,
                                Role expected_role) {
  UpdaterConfig config;
  const Json::Value root =
      ParseJsonString(contents, "anchored updater configuration");

  config.repo = JsonString(root, "repo", config.repo);
  config.channel = JsonString(root, "channel", config.channel);
  config.role =
      JsonString(root, "role", std::string(RoleToCString(expected_role)));
  if (!IsTrimmedPrintableAscii(config.repo) ||
      !IsTrimmedPrintableAscii(config.channel))
    throw std::runtime_error(
        "Updater repo and channel must be trimmed printable ASCII");
  if (config.role != RoleToCString(expected_role))
    throw std::runtime_error(
        "Updater configuration role does not match the active launcher");
  config.release_index_asset = RequireSafeUpdaterFileName(
      JsonString(root, "release_index_asset",
                 "worr-release-index-" + config.channel + ".json"),
      "release index asset name");
  config.launch_exe =
      JsonStringAny(root, {"launch_exe", "launcher_exe"}, config.launch_exe);
  config.autolaunch = JsonBool(root, "autolaunch", config.autolaunch);
  config.allow_prerelease =
      JsonBool(root, "allow_prerelease", config.allow_prerelease);
  config.allow_unsigned_development_index =
      JsonBool(root, "allow_unsigned_development_index",
               config.allow_unsigned_development_index);
  if (root.isMember("release_trust_policy"))
    config.release_trust_policy =
        ParseReleaseTrustPolicy(root["release_trust_policy"]);
  if (config.allow_unsigned_development_index &&
      (config.channel != "development" ||
       config.release_trust_policy.has_value()))
    throw std::runtime_error(
        "Unsigned release indices are restricted to an explicit development "
        "channel without a trust policy");
  if (config.release_trust_policy) {
    const size_t authorized = static_cast<size_t>(std::count_if(
        config.release_trust_policy->keys.begin(),
        config.release_trust_policy->keys.end(),
        [&](const TrustedReleaseKey &key) {
          return std::find(key.repos.begin(), key.repos.end(), config.repo) !=
                     key.repos.end() &&
                 std::find(key.channels.begin(), key.channels.end(),
                           config.channel) != key.channels.end();
        }));
    if (authorized < config.release_trust_policy->threshold)
      throw std::runtime_error("Release trust policy threshold cannot be met "
                               "for the configured repo/channel");
  }
  if (root.isMember("preserve") && root["preserve"].isArray()) {
    config.preserve.clear();
    for (const Json::Value &item : root["preserve"]) {
      if (item.isString())
        config.preserve.push_back(item.asString());
    }
  }
  return config;
}

InstallManifest LoadInstallManifest(const std::string &contents,
                                    const UpdaterConfig &config,
                                    const BootstrapOptions &options) {
  InstallManifest manifest;
  const Json::Value root =
      ParseJsonString(contents, "anchored local install manifest");
  manifest.version = JsonString(root, "version");
  manifest.role = JsonString(root, "role");
  manifest.launch_exe = JsonStringAny(root, {"launch_exe", "launcher_exe"});
  manifest.engine_library =
      JsonStringAny(root, {"engine_library", "runtime_exe"});
  manifest.local_manifest_name =
      JsonString(root, "local_manifest_name", kLocalManifestName);
  if (root.isMember("files") && root["files"].isArray()) {
    if (root["files"].size() > kMaxManagedFiles)
      throw std::runtime_error(
          "Local install manifest has too many managed files");
    for (const Json::Value &entry : root["files"]) {
      if (!entry.isObject())
        continue;
      FileEntry file;
      file.path = JsonString(entry, "path");
      file.sha256 = JsonString(entry, "sha256");
      file.size = JsonUInt64(entry, "size", 0);
      const uint64_t mode = JsonUInt64(entry, "mode", 0);
      if (mode == 0644 || mode == 0755)
        file.mode = static_cast<uint32_t>(mode);
      if (!file.path.empty()) {
        file.path =
            RequireSafeUpdaterPath(file.path, "local manifest file path");
        manifest.files.push_back(std::move(file));
      }
    }
  }

  try {
    const Json::Value &authentication =
        root["installed_release_authentication"];
    if (!authentication.isObject() ||
        JsonUInt64(authentication, "schema_version", 0) != 1)
      throw std::runtime_error(
          "local manifest has no authenticated installed-release evidence");

    RemotePayload authenticated_payload;
    authenticated_payload.release_index_name =
        JsonString(authentication, "release_index_name");
    authenticated_payload.raw_release_index =
        JsonString(authentication, "raw_release_index");
    authenticated_payload.raw_release_signature =
        JsonString(authentication, "raw_release_signature");
    authenticated_payload.raw_update_manifest =
        JsonString(authentication, "raw_update_manifest");
    RevalidateCachedRemotePayload(&authenticated_payload, config, options,
                                  false);
    if (BuildLocalInstallManifest(authenticated_payload) != root)
      throw std::runtime_error(
          "local manifest does not match its authenticated release evidence");
    manifest.authenticated_files = true;
    manifest.authenticated_release_sequence =
        authenticated_payload.release_sequence;
    manifest.authenticated_release_index_sha256 =
        authenticated_payload.release_index_sha256;
  } catch (const std::exception &error) {
    BootstrapTrace("Local install manifest is not authenticated for removal: " +
                   std::string(error.what()));
  }
  return manifest;
}

bool IsSha256Hex(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

struct ReleaseIndexSecurityMetadata {
  uint64_t sequence = 0;
  std::string expires_at_utc;
  std::vector<std::string> revoked_index_sha256s;
};

int ParseUtcField(std::string_view value, size_t offset, size_t length) {
  int result = 0;
  for (size_t index = 0; index < length; ++index) {
    const char character = value[offset + index];
    if (character < '0' || character > '9')
      throw std::runtime_error("Release index timestamp is invalid");
    result = result * 10 + (character - '0');
  }
  return result;
}

std::time_t ParseReleaseUtcTimestamp(std::string_view value) {
  if (value.size() != 20 || value[4] != '-' || value[7] != '-' ||
      value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
      value[19] != 'Z')
    throw std::runtime_error("Release index timestamp is not canonical UTC");

  std::tm utc{};
  utc.tm_year = ParseUtcField(value, 0, 4) - 1900;
  utc.tm_mon = ParseUtcField(value, 5, 2) - 1;
  utc.tm_mday = ParseUtcField(value, 8, 2);
  utc.tm_hour = ParseUtcField(value, 11, 2);
  utc.tm_min = ParseUtcField(value, 14, 2);
  utc.tm_sec = ParseUtcField(value, 17, 2);
  utc.tm_isdst = 0;
#if defined(_WIN32)
  const std::time_t parsed = _mkgmtime(&utc);
#else
  const std::time_t parsed = timegm(&utc);
#endif
  if (parsed == static_cast<std::time_t>(-1))
    throw std::runtime_error(
        "Release index timestamp is outside the supported range");

  std::tm normalized{};
#if defined(_WIN32)
  gmtime_s(&normalized, &parsed);
#else
  gmtime_r(&parsed, &normalized);
#endif
  char canonical[21]{};
  if (std::strftime(canonical, sizeof(canonical), "%Y-%m-%dT%H:%M:%SZ",
                    &normalized) != 20 ||
      value != canonical)
    throw std::runtime_error(
        "Release index timestamp is not a real UTC instant");
  return parsed;
}

ReleaseIndexSecurityMetadata
ValidateReleaseIndexSecurityMetadata(const Json::Value &release_index,
                                     bool require_current_freshness = true) {
  ReleaseIndexSecurityMetadata metadata;
  metadata.sequence = JsonUInt64(release_index, "release_sequence", 0);
  metadata.expires_at_utc = JsonString(release_index, "expires_at_utc");
  const std::string generated_at_utc =
      JsonString(release_index, "generated_at_utc");
  if (metadata.sequence == 0 || generated_at_utc.empty() ||
      metadata.expires_at_utc.empty())
    throw std::runtime_error(
        "Release index is missing signed freshness metadata");

  const std::time_t generated = ParseReleaseUtcTimestamp(generated_at_utc);
  const std::time_t expires = ParseReleaseUtcTimestamp(metadata.expires_at_utc);
  const std::time_t now = std::time(nullptr);
  if (expires <= generated ||
      std::difftime(expires, generated) >
          static_cast<double>(kMaximumReleaseIndexLifetimeSeconds))
    throw std::runtime_error("Release index validity interval is invalid");
  if (require_current_freshness) {
    if (std::difftime(generated, now) >
        static_cast<double>(kReleaseClockSkewSeconds))
      throw std::runtime_error(
          "Release index was generated too far in the future");
    if (std::difftime(now, expires) >
        static_cast<double>(kReleaseClockSkewSeconds))
      throw std::runtime_error("Release index has expired");
  }

  const Json::Value &revoked = release_index["revoked_release_index_sha256s"];
  if (!revoked.isArray() || revoked.size() > kMaxRevokedReleaseIndices)
    throw std::runtime_error("Release index revocation metadata is invalid");
  std::set<std::string> distinct;
  for (const Json::Value &value : revoked) {
    if (!value.isString() || !IsSha256Hex(value.asString()) ||
        !distinct.insert(value.asString()).second)
      throw std::runtime_error(
          "Release index contains an invalid or duplicate revocation");
    metadata.revoked_index_sha256s.push_back(value.asString());
  }
  return metadata;
}

Json::Value AssetIntegrityToJson(const AssetIntegrity &integrity) {
  Json::Value value(Json::objectValue);
  value["sha256"] = integrity.sha256;
  value["size"] = Json::UInt64(integrity.size);
  return value;
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
  Json::Value release_index(Json::objectValue);
  release_index["name"] = payload.release_index_name;
  release_index["sha256"] = payload.release_index_sha256;
  release_index["raw_index"] = payload.raw_release_index;
  release_index["raw_signature"] = payload.raw_release_signature;
  release_index["release_sequence"] = Json::UInt64(payload.release_sequence);
  release_index["expires_at_utc"] = payload.release_expires_at_utc;
  Json::Value revoked_index_sha256s(Json::arrayValue);
  for (const std::string &sha256 : payload.revoked_release_index_sha256s)
    revoked_index_sha256s.append(sha256);
  release_index["revoked_release_index_sha256s"] =
      std::move(revoked_index_sha256s);
  release_index["unsigned_development"] = payload.unsigned_development_index;
  Json::Value verified_key_ids(Json::arrayValue);
  for (const std::string &key_id : payload.verified_release_key_ids)
    verified_key_ids.append(key_id);
  release_index["verified_key_ids"] = std::move(verified_key_ids);
  root["release_index_verification"] = std::move(release_index);
  root["update_manifest_integrity"] =
      AssetIntegrityToJson(payload.update_manifest_integrity);
  root["update_package_integrity"] =
      AssetIntegrityToJson(payload.update_package_integrity);
  root["raw_update_manifest"] = payload.raw_update_manifest;
  root["manifest"] = payload.manifest_json;
  return root;
}

AssetIntegrity AssetIntegrityFromJson(const Json::Value &value,
                                      std::string_view description) {
  if (!value.isObject())
    throw std::runtime_error("Pending " + std::string(description) +
                             " is not an object");
  AssetIntegrity integrity;
  integrity.sha256 = JsonString(value, "sha256");
  integrity.size = JsonUInt64(value, "size", 0);
  if (!IsSha256Hex(integrity.sha256) || integrity.size == 0)
    throw std::runtime_error("Pending " + std::string(description) +
                             " is invalid");
  return integrity;
}

void ValidateRemotePayloadManifest(const RemotePayload &payload) {
  const Json::Value &manifest = payload.manifest_json;
  if (!manifest.isObject())
    throw std::runtime_error("Pending update manifest is not an object");
  if (JsonUInt64(manifest, "schema_version", 0) != 3)
    throw std::runtime_error(
        "Pending update manifest uses an unsupported schema");

  const std::array<std::pair<const char *, std::string>, 5> identity = {{
      {"version", payload.version},
      {"role", payload.role},
      {"launch_exe", payload.launch_exe},
      {"engine_library", payload.engine_library},
      {"local_manifest_name", payload.local_manifest_name},
  }};
  for (const auto &[key, expected] : identity) {
    if (expected.empty() || JsonString(manifest, key) != expected)
      throw std::runtime_error("Pending update manifest " + std::string(key) +
                               " mismatch");
  }

  const Json::Value &package = manifest["package"];
  if (!package.isObject() ||
      JsonString(package, "name") != payload.update_package_name ||
      JsonString(package, "format") != "zip")
    throw std::runtime_error(
        "Pending update manifest package contract mismatch");
  const Json::Value &package_size = package["size"];
  if ((!package_size.isUInt() && !package_size.isUInt64()) ||
      package_size.asUInt64() == 0 ||
      !IsSha256Hex(JsonString(package, "sha256")))
    throw std::runtime_error(
        "Pending update manifest has invalid package integrity metadata");
  if (package_size.asUInt64() > kMaxUpdatePackageBytes)
    throw std::runtime_error(
        "Pending update package exceeds the supported size limit");
  if (package_size.asUInt64() != payload.update_package_integrity.size ||
      JsonString(package, "sha256") != payload.update_package_integrity.sha256)
    throw std::runtime_error("Pending update manifest package integrity does "
                             "not match the signed release index");

  const Json::Value &files = manifest["files"];
  if (!files.isArray() || files.empty())
    throw std::runtime_error("Pending update manifest has no managed files");
  if (files.size() > kMaxManagedFiles)
    throw std::runtime_error(
        "Pending update manifest has too many managed files");

  std::map<std::string, std::string> seen_paths;
  uint64_t expanded_size = 0;
  for (const Json::Value &value : files) {
    if (!value.isObject())
      throw std::runtime_error(
          "Pending update manifest has an invalid file entry");
    const std::string source_path = JsonString(value, "path");
    const std::string path =
        RequireSafeUpdaterPath(source_path, "pending manifest file path");
    const Json::Value &size = value["size"];
    const Json::Value &mode = value["mode"];
    if ((!size.isUInt() && !size.isUInt64()) ||
        !IsSha256Hex(JsonString(value, "sha256")) ||
        (!mode.isUInt() && !mode.isUInt64()) ||
        (mode.asUInt64() != 0644 && mode.asUInt64() != 0755))
      throw std::runtime_error(
          "Pending update manifest has invalid file integrity metadata: " +
          path);
    const uint64_t file_size = size.asUInt64();
    if (file_size > kMaxUpdateFileBytes ||
        expanded_size > kMaxExpandedUpdateBytes - file_size)
      throw std::runtime_error(
          "Pending update manifest exceeds expanded payload limits");
    expanded_size += file_size;

    const std::string key = UpdaterPathCollisionKey(path);
    if (key == ".worr-update" || key.rfind(".worr-update/", 0) == 0)
      throw std::runtime_error(
          "Pending update targets reserved updater state: " + path);
    if (key == UpdaterPathCollisionKey(payload.local_manifest_name))
      throw std::runtime_error(
          "Pending update file list aliases the local install manifest: " +
          path);
    if (!seen_paths.emplace(key, path).second)
      throw std::runtime_error(
          "Pending update manifest has duplicate file path: " + path);
  }
  for (auto current = seen_paths.begin(); current != seen_paths.end(); ++current) {
    const auto next = std::next(current);
    if (next != seen_paths.end() &&
        next->first.rfind(current->first + "/", 0) == 0)
      throw std::runtime_error(
          "Pending update manifest has a file/directory prefix collision: " +
          current->second + " and " + next->second);
  }
}

void ValidateReleaseVerificationContext(const RemotePayload &payload,
                                        const UpdaterConfig &config) {
  if (payload.release_index_name != config.release_index_asset ||
      !IsSha256Hex(payload.release_index_sha256))
    throw std::runtime_error("Pending update release-index identity does not "
                             "match the active configuration");
  if (payload.unsigned_development_index) {
    if (!config.allow_unsigned_development_index ||
        config.channel != "development" ||
        !payload.verified_release_key_ids.empty())
      throw std::runtime_error("Pending unsigned update is not authorized by "
                               "the active development configuration");
    return;
  }

  if (!config.release_trust_policy)
    throw std::runtime_error("Pending update cannot be authenticated because "
                             "no release trust policy is configured");

  std::set<std::string> distinct_ids;
  for (const std::string &key_id : payload.verified_release_key_ids) {
    if (!IsSha256Hex(key_id) || !distinct_ids.insert(key_id).second)
      throw std::runtime_error(
          "Pending update has invalid release verification key identities");
  }

  size_t authorized = 0;
  for (const TrustedReleaseKey &key : config.release_trust_policy->keys) {
    if (distinct_ids.find(key.key_id) != distinct_ids.end() &&
        std::find(key.repos.begin(), key.repos.end(), config.repo) !=
            key.repos.end() &&
        std::find(key.channels.begin(), key.channels.end(), config.channel) !=
            key.channels.end())
      ++authorized;
  }
  if (authorized < config.release_trust_policy->threshold)
    throw std::runtime_error("Pending update no longer meets the active "
                             "release-signature threshold");
}

void ValidateRemotePayloadContext(const RemotePayload &payload,
                                  const UpdaterConfig &config,
                                  const BootstrapOptions &options) {
  ValidateReleaseVerificationContext(payload, config);
  const std::string expected_role = RoleToCString(options.role);
  if (payload.role != expected_role)
    throw std::runtime_error(
        "Pending update role does not match the active launcher");
  if (JsonString(payload.manifest_json, "repo") != config.repo)
    throw std::runtime_error(
        "Pending update repository does not match the active configuration");
  if (JsonString(payload.manifest_json, "channel") != config.channel)
    throw std::runtime_error(
        "Pending update channel does not match the active configuration");

  const std::string expected_launch =
      RequireSafeUpdaterPath(options.launch_relpath, "active launch path");
  const std::string expected_engine = RequireSafeUpdaterPath(
      options.engine_library_relpath, "active engine path");
  if (payload.launch_exe != expected_launch)
    throw std::runtime_error(
        "Pending update launch path does not match the active launcher");
  if (payload.engine_library != expected_engine)
    throw std::runtime_error(
        "Pending update engine path does not match the active launcher");
}

void ValidateReleaseSequenceAgainstInstalled(const RemotePayload &payload,
                                             const InstallManifest &installed) {
  if (!installed.authenticated_files)
    return;
  if (payload.release_sequence < installed.authenticated_release_sequence)
    throw std::runtime_error(
        "Release index sequence is older than the authenticated install");
  if (payload.release_sequence == installed.authenticated_release_sequence &&
      payload.release_index_sha256 !=
          installed.authenticated_release_index_sha256)
    throw std::runtime_error(
        "Release index sequence conflicts with the authenticated install");
}

void ValidateApprovedReleaseBinding(const RemotePayload &payload,
                                    const BootstrapOptions &options) {
  if (!options.approved_install)
    return;
  if (payload.release_index_sha256 != options.approved_release_index_sha256 ||
      payload.release_sequence != options.approved_release_sequence)
    throw std::runtime_error(
        "Available release does not match the exact update approved by the "
        "user");
}

void RevalidateRemotePayloadAtUse(RemotePayload *payload,
                                  const UpdaterConfig &config,
                                  const BootstrapOptions &options,
                                  const InstallManifest &installed) {
  RevalidateCachedRemotePayload(payload, config, options);
  ValidateRemotePayloadContext(*payload, config, options);
  ValidateApprovedReleaseBinding(*payload, options);
  ValidateReleaseSequenceAgainstInstalled(*payload, installed);
}

std::optional<RemotePayload> RemotePayloadFromJson(const Json::Value &root) {
  if (!root.isObject())
    return std::nullopt;

  RemotePayload payload;
  payload.version = JsonString(root, "version");
  payload.tag = JsonString(root, "tag");
  payload.role = JsonString(root, "role");
  payload.manifest_url = JsonString(root, "manifest_url");
  payload.package_url = JsonString(root, "package_url");
  if (payload.version.empty() || payload.package_url.empty() ||
      payload.manifest_url.empty())
    return std::nullopt;

  payload.launch_exe = RequireSafeUpdaterPath(
      JsonStringAny(root, {"launch_exe", "launcher_exe"}),
      "pending launch path");
  payload.engine_library = RequireSafeUpdaterPath(
      JsonStringAny(root, {"engine_library", "runtime_exe"}),
      "pending engine path");
  payload.update_manifest_name = RequireSafeUpdaterFileName(
      JsonString(root, "update_manifest_name"), "pending manifest asset name");
  payload.update_package_name = RequireSafeUpdaterFileName(
      JsonString(root, "update_package_name"), "pending package asset name");
  payload.local_manifest_name = RequireSafeUpdaterFileName(
      JsonString(root, "local_manifest_name", kLocalManifestName),
      "pending local manifest name");
  const Json::Value &release_index = root["release_index_verification"];
  if (!release_index.isObject())
    return std::nullopt;
  payload.release_index_name = RequireSafeUpdaterFileName(
      JsonString(release_index, "name"), "pending release index asset name");
  payload.release_index_sha256 = JsonString(release_index, "sha256");
  payload.raw_release_index = JsonString(release_index, "raw_index");
  payload.raw_release_signature = JsonString(release_index, "raw_signature");
  payload.unsigned_development_index =
      JsonBool(release_index, "unsigned_development", false);
  const Json::Value &verified_key_ids = release_index["verified_key_ids"];
  if (!verified_key_ids.isArray())
    return std::nullopt;
  std::set<std::string> seen_key_ids;
  for (const Json::Value &value : verified_key_ids) {
    if (!value.isString() || !IsSha256Hex(value.asString()) ||
        !seen_key_ids.insert(value.asString()).second)
      throw std::runtime_error(
          "Pending release index has invalid verified key identities");
    payload.verified_release_key_ids.push_back(value.asString());
  }
  if (payload.unsigned_development_index !=
      payload.verified_release_key_ids.empty())
    throw std::runtime_error(
        "Pending release index has inconsistent verification metadata");
  if (!IsSha256Hex(payload.release_index_sha256))
    throw std::runtime_error(
        "Pending release index has invalid integrity metadata");
  payload.update_manifest_integrity = AssetIntegrityFromJson(
      root["update_manifest_integrity"], "update manifest integrity");
  payload.update_package_integrity = AssetIntegrityFromJson(
      root["update_package_integrity"], "update package integrity");
  payload.raw_update_manifest = JsonString(root, "raw_update_manifest");
  if (payload.raw_release_index.empty() || payload.raw_update_manifest.empty())
    return std::nullopt;
  payload.manifest_json = root["manifest"];
  return payload;
}

class BootstrapUi {
public:
  virtual ~BootstrapUi() = default;
  virtual bool SetStatus(const std::string &headline,
                         const std::string &detail = {}) = 0;
  virtual bool SetProgress(const std::string &label, uint64_t current,
                           uint64_t total) = 0;
  virtual bool Pump() = 0;
  virtual bool PromptInstall(const std::string &headline,
                             const std::string &detail) = 0;
  virtual bool WaitForMinimumDisplayTime() { return true; }
  virtual void DismissForEngineHandoff() = 0;
  virtual bool SupportsSharedWindowHandoff() const { return false; }
  virtual bool PrepareSharedWindowHandoff() { return false; }
  virtual void *GetSharedWindowNativeHandle() { return nullptr; }
};

class ConsoleUi final : public BootstrapUi {
public:
  bool SetStatus(const std::string &headline,
                 const std::string &detail = {}) override {
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

  bool SetProgress(const std::string &label, uint64_t current,
                   uint64_t total) override {
    if (total == 0)
      return true;
    const int percent =
        static_cast<int>((current * 100u) / std::max<uint64_t>(1, total));
    if (label != last_progress_label_ || percent >= last_percent_ + 10 ||
        percent == 100) {
      std::cout << label << ": " << percent << "%" << std::endl;
      last_progress_label_ = label;
      last_percent_ = percent;
    }
    return true;
  }

  bool Pump() override { return true; }

  bool PromptInstall(const std::string &headline,
                     const std::string &detail) override {
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
  bool SetStatus(const std::string &, const std::string & = {}) override {
    return true;
  }
  bool SetProgress(const std::string &, uint64_t, uint64_t) override {
    return true;
  }
  bool Pump() override { return true; }
  bool PromptInstall(const std::string &, const std::string &) override {
    return false;
  }
  void DismissForEngineHandoff() override {}
};

class SplashUi final : public BootstrapUi {
public:
  explicit SplashUi(SessionShellWindowConfig config)
      : config_(std::move(config)) {
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
      if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
      owns_sdl_video_ = true;
    }

    placement_ = ResolveSessionShellPlacement(config_);
    CreateShellWindow();
    ApplySessionShellWindowMode();
    if (!SDL_ShowWindow(window_))
      throw std::runtime_error(SDL_GetError());

    renderer_ = SDL_CreateRenderer(window_,
#if defined(_WIN32)
                                   "software"
#else
                                   nullptr
#endif
    );
    if (!renderer_)
      renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
      throw std::runtime_error(SDL_GetError());

    SDL_Surface *surface = SDL_CreateSurfaceFrom(
        generated::kBootstrapLogoWidth, generated::kBootstrapLogoHeight,
        SDL_PIXELFORMAT_RGBA32,
        const_cast<uint8_t *>(generated::kBootstrapLogoRgba),
        generated::kBootstrapLogoWidth * 4);
    if (!surface)
      throw std::runtime_error(SDL_GetError());

    logo_ = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (!logo_)
      throw std::runtime_error(SDL_GetError());

    InitializeTextRenderer();
  }

  ~SplashUi() override { ReleaseUiResources(false); }

  bool SetStatus(const std::string &headline,
                 const std::string &detail = {}) override {
    headline_ = headline;
    detail_ = detail;
    Render();
    return !closed_;
  }

  bool SetProgress(const std::string &label, uint64_t current,
                   uint64_t total) override {
    const bool label_changed = progress_label_ != label;
    progress_label_ = label;
    progress_current_ = current;
    progress_total_ = total;
    if (progress_total_ > 0) {
      const float target = std::clamp(static_cast<float>(progress_current_) /
                                          static_cast<float>(progress_total_),
                                      0.0f, 1.0f);
      if (label_changed || target < progress_display_) {
        progress_display_ = target;
      }
      progress_target_ = target;
    } else if (label_changed) {
      progress_display_ = 0.0f;
      progress_target_ = 0.0f;
    }
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
      } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                 event.button.button == SDL_BUTTON_LEFT) {
        const float x = event.button.x;
        const float y = event.button.y;
        if (x >= install_button_.x &&
            x <= install_button_.x + install_button_.w &&
            y >= install_button_.y &&
            y <= install_button_.y + install_button_.h) {
          prompt_result_ = true;
        } else if (x >= exit_button_.x &&
                   x <= exit_button_.x + exit_button_.w &&
                   y >= exit_button_.y &&
                   y <= exit_button_.y + exit_button_.h) {
          prompt_result_ = false;
        }
      }
    }
    AnimateProgress();
    Render();
    SDL_Delay(kUiTickDelayMs);
    return !closed_;
  }

  bool PromptInstall(const std::string &headline,
                     const std::string &detail) override {
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

  bool WaitForMinimumDisplayTime() override {
    while (!closed_) {
      const uint64_t now = SDL_GetTicks();
      if (now >= created_at_ + kMinimumSplashDisplayMs)
        return true;
      if (!Pump())
        return false;
    }
    return false;
  }

  void DismissForEngineHandoff() override {
    closed_ = true;
    ReleaseUiResources(true);
  }

  bool SupportsSharedWindowHandoff() const override {
#if defined(_WIN32)
    // The native splash shell is an updater/bootstrap surface, not a renderer
    // swapchain owner. Keeping it as the final app HWND leaves Windows 11
    // taskbar thumbnails and OS PrintScreen sampling stale bootstrap pixels, so
    // let the engine create and own its real window for now.
    return false;
#else
    return false;
#endif
  }

  bool PrepareSharedWindowHandoff() override {
#if defined(_WIN32)
    if (!native_window_)
      return false;
    PresentHandoffFrame();
    closed_ = true;
    ReleaseUiResources(true, true);
    return native_window_ != nullptr;
#else
    return false;
#endif
  }

  void *GetSharedWindowNativeHandle() override {
#if defined(_WIN32)
    if (!native_window_) {
      BootstrapTrace("SplashUi shared_hwnd hwnd_missing");
      return nullptr;
    }
    void *hwnd = native_window_;
    BootstrapTrace("SplashUi shared_hwnd props_ok hwnd=" + PointerString(hwnd));
    return hwnd;
#else
    return nullptr;
#endif
  }

private:
  static constexpr float kHeadlineTextMinPx = 22.0f;
  static constexpr float kHeadlineTextMaxPx = 34.0f;
  static constexpr float kDetailTextMinPx = 15.0f;
  static constexpr float kDetailTextMaxPx = 22.0f;
  static constexpr float kProgressTextMinPx = 14.0f;
  static constexpr float kProgressTextMaxPx = 20.0f;
  static constexpr float kButtonTextMinPx = 15.0f;
  static constexpr float kButtonTextMaxPx = 20.0f;
  static constexpr float kLegalTextMinPx = 12.0f;
  static constexpr float kLegalTextMaxPx = 16.0f;
  static constexpr float kButtonWidth = 132.0f;
  static constexpr float kButtonHeight = 36.0f;
  static constexpr float kButtonGap = 16.0f;
  static constexpr const char *kLegalText =
      "(c) DarkMatter Productions, 2026.\n"
      "Quake, Quake II, id Software, Bethesda, and related marks are the "
      "property of id Software LLC and/or ZeniMax Media Inc.\n"
      "WORR is an unofficial fan project and is not affiliated with or "
      "endorsed by id Software or Bethesda.";

#if defined(_WIN32)
  static void FlushDesktopComposition() {
    GdiFlush();

    using dwm_flush_fn = HRESULT(WINAPI *)(void);
    static HMODULE dwmapi = LoadLibraryA("dwmapi.dll");
    static auto dwm_flush =
        dwmapi
            ? reinterpret_cast<dwm_flush_fn>(GetProcAddress(dwmapi, "DwmFlush"))
            : nullptr;
    if (dwm_flush)
      dwm_flush();
  }

  static LRESULT CALLBACK NativeShellWindowProc(HWND hwnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    default:
      break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
  }

  static const char *NativeShellWindowClassName() {
    return "WORRBootstrapShellWindow";
  }

  static void EnsureNativeShellWindowClass() {
    static bool registered = false;
    if (registered)
      return;

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = NativeShellWindowProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = NativeShellWindowClassName();

    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      throw std::runtime_error(
          "Failed registering bootstrap shell window class");

    registered = true;
  }

  RECT ResolveMonitorRect(bool fullscreen) const {
    RECT rect{};
    if (placement_.has_bounds) {
      rect.left = placement_.bounds.x;
      rect.top = placement_.bounds.y;
      rect.right = placement_.bounds.x + placement_.bounds.w;
      rect.bottom = placement_.bounds.y + placement_.bounds.h;
      return rect;
    }

    if (!fullscreen) {
      SystemParametersInfoA(SPI_GETWORKAREA, 0, &rect, 0);
      if (rect.right > rect.left && rect.bottom > rect.top)
        return rect;
    }

    rect.left = 0;
    rect.top = 0;
    rect.right = GetSystemMetrics(SM_CXSCREEN);
    rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    return rect;
  }

  void ApplyNativeWindowMode() {
    if (!native_window_)
      return;

    const bool fullscreen =
        config_.mode == SessionShellWindowMode::BorderlessFullscreen ||
        config_.mode == SessionShellWindowMode::ExclusiveFullscreen ||
        config_.mode == SessionShellWindowMode::SpanBorderless;
    const bool borderless =
        fullscreen || config_.mode == SessionShellWindowMode::BorderlessWindow;
    DWORD style =
        borderless ? (WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
                   : (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    DWORD exstyle = WS_EX_TOOLWINDOW;

    RECT monitor_rect = ResolveMonitorRect(fullscreen);
    int client_width = fullscreen ? (monitor_rect.right - monitor_rect.left)
                                  : placement_.width;
    int client_height = fullscreen ? (monitor_rect.bottom - monitor_rect.top)
                                   : placement_.height;
    int x = fullscreen ? monitor_rect.left : placement_.x;
    int y = fullscreen ? monitor_rect.top : placement_.y;

    if (!fullscreen) {
      if (SDL_WINDOWPOS_ISCENTERED(x))
        x = monitor_rect.left +
            ((monitor_rect.right - monitor_rect.left) - client_width) / 2;
      if (SDL_WINDOWPOS_ISCENTERED(y))
        y = monitor_rect.top +
            ((monitor_rect.bottom - monitor_rect.top) - client_height) / 2;
    }

    RECT rect{0, 0, client_width, client_height};
    AdjustWindowRectEx(&rect, style, FALSE, exstyle);
    const int window_width = rect.right - rect.left;
    const int window_height = rect.bottom - rect.top;

    SetWindowLongPtrA(native_window_, GWL_STYLE, static_cast<LONG_PTR>(style));
    SetWindowLongPtrA(native_window_, GWL_EXSTYLE,
                      static_cast<LONG_PTR>(exstyle));
    SetWindowPos(native_window_, fullscreen ? HWND_TOP : HWND_NOTOPMOST, x, y,
                 window_width, window_height,
                 SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER |
                     SWP_HIDEWINDOW);
    UpdateWindow(native_window_);
  }

  void CreateNativeShellWindow() {
    EnsureNativeShellWindowClass();

    native_window_ = CreateWindowExA(
        WS_EX_TOOLWINDOW, NativeShellWindowClassName(), "WORR",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CW_USEDEFAULT,
        CW_USEDEFAULT, placement_.width, placement_.height, nullptr, nullptr,
        GetModuleHandleA(nullptr), nullptr);
    if (!native_window_)
      throw std::runtime_error("Failed creating bootstrap shell window");

    ApplyNativeWindowMode();
  }

  void WrapNativeShellWindow() {
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props)
      throw std::runtime_error(SDL_GetError());

    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    SDL_SetBooleanProperty(
        props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER,
                           native_window_);

    window_ = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window_)
      throw std::runtime_error(SDL_GetError());

    wrapped_external_window_ = true;
  }
#endif

  void CreateShellWindow() {
#if defined(_WIN32)
    CreateNativeShellWindow();
    WrapNativeShellWindow();
#else
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props)
      throw std::runtime_error(SDL_GetError());

    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "WORR");
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    SDL_SetBooleanProperty(
        props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,
                           config_.mode == SessionShellWindowMode::Windowed);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
                          placement_.width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                          placement_.height);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, placement_.x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, placement_.y);

    window_ = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window_)
      throw std::runtime_error(SDL_GetError());
#endif
  }

  bool ApplySdlChange(bool result, const char *step) {
    if (!result) {
      BootstrapTrace("SplashUi step=" + std::string(step) +
                     " error=" + SDL_GetError());
      return false;
    }
    return true;
  }

  bool ApplyWindowedMode(bool bordered = true) {
    bool ok = true;
    ok &= ApplySdlChange(SDL_SetWindowFullscreen(window_, false),
                         "leave_fullscreen");
    ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, nullptr),
                         "clear_fullscreen_mode");
    ok &= ApplySdlChange(SDL_SetWindowBordered(window_, bordered),
                         bordered ? "set_bordered" : "set_borderless");
    ok &= ApplySdlChange(
        SDL_SetWindowSize(window_, config_.width, config_.height),
        "set_window_size");
    if (config_.geometry_specified) {
      ok &= ApplySdlChange(SDL_SetWindowPosition(window_, config_.x, config_.y),
                           "set_window_position");
    }
    ok &= ApplySdlChange(SDL_SyncWindow(window_), "sync_window");
    return ok;
  }

  bool ApplySpanBorderlessMode() {
    if (!placement_.has_bounds)
      return false;

    bool ok = true;
    ok &= ApplySdlChange(SDL_SetWindowFullscreen(window_, false),
                         "leave_fullscreen");
    ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, nullptr),
                         "clear_fullscreen_mode");
    ok &=
        ApplySdlChange(SDL_SetWindowBordered(window_, false), "set_borderless");
    ok &= ApplySdlChange(SDL_SetWindowPosition(window_, placement_.bounds.x,
                                               placement_.bounds.y),
                         "set_span_position");
    ok &= ApplySdlChange(
        SDL_SetWindowSize(window_, placement_.bounds.w, placement_.bounds.h),
        "set_span_size");
    ok &= ApplySdlChange(SDL_SyncWindow(window_), "sync_window");
    return ok;
  }

  bool ApplyBorderlessFullscreenMode() {
    if (placement_.has_bounds) {
      ApplySdlChange(SDL_SetWindowPosition(window_, placement_.bounds.x,
                                           placement_.bounds.y),
                     "set_fullscreen_position");
      ApplySdlChange(
          SDL_SetWindowSize(window_, placement_.bounds.w, placement_.bounds.h),
          "set_fullscreen_size");
    }

    bool ok = true;
    ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, nullptr),
                         "clear_fullscreen_mode");
    ok &= ApplySdlChange(SDL_SetWindowFullscreen(window_, true),
                         "enter_borderless_fullscreen");
    ok &= ApplySdlChange(SDL_SyncWindow(window_), "sync_window");
    return ok;
  }

  bool ApplyExclusiveFullscreenMode() {
    bool ok = true;
    if (placement_.has_bounds) {
      ok &= ApplySdlChange(SDL_SetWindowPosition(window_, placement_.bounds.x,
                                                 placement_.bounds.y),
                           "set_exclusive_position");
    }

    const std::optional<SDL_DisplayMode> display_mode =
        ResolveExclusiveDisplayMode(placement_.display_id,
                                    config_.fullscreen_index);
    if (display_mode.has_value()) {
      ok &= ApplySdlChange(
          SDL_SetWindowSize(window_, display_mode->w, display_mode->h),
          "set_exclusive_size");
      ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, &*display_mode),
                           "set_exclusive_mode");
    } else {
      if (placement_.has_bounds) {
        ok &= ApplySdlChange(SDL_SetWindowSize(window_, placement_.bounds.w,
                                               placement_.bounds.h),
                             "set_exclusive_size");
      }
      ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, nullptr),
                           "clear_fullscreen_mode");
    }

    ok &= ApplySdlChange(SDL_SetWindowFullscreen(window_, true),
                         "enter_exclusive_fullscreen");
    ok &= ApplySdlChange(SDL_SyncWindow(window_), "sync_window");
    return ok;
  }

  void ApplySessionShellWindowMode() {
#if defined(_WIN32)
    if (wrapped_external_window_) {
      ApplyNativeWindowMode();
      const char *display_name = placement_.display_id
                                     ? SDL_GetDisplayName(placement_.display_id)
                                     : nullptr;
      std::ostringstream detail;
      detail << "SplashUi window_mode="
             << SessionShellWindowModeToCString(config_.mode) << " display_id="
             << static_cast<unsigned long long>(placement_.display_id)
             << " invalid_display=" << (placement_.invalid_display ? 1 : 0)
             << " fallback=0"
             << " bounds=" << placement_.bounds.w << 'x' << placement_.bounds.h
             << std::showpos << placement_.bounds.x << placement_.bounds.y
             << std::noshowpos << " create=" << placement_.width << 'x'
             << placement_.height << " native_wrap=1";
      if (display_name && *display_name)
        detail << " name=\"" << display_name << '"';
      BootstrapTrace(detail.str());
      return;
    }
#endif
    bool ok = false;
    bool fell_back = false;
    switch (config_.mode) {
    case SessionShellWindowMode::Windowed:
      ok = ApplyWindowedMode();
      break;
    case SessionShellWindowMode::BorderlessWindow:
      ok = ApplyWindowedMode(false);
      break;
    case SessionShellWindowMode::BorderlessFullscreen:
      ok = ApplyBorderlessFullscreenMode();
      break;
    case SessionShellWindowMode::ExclusiveFullscreen:
      ok = ApplyExclusiveFullscreenMode();
      break;
    case SessionShellWindowMode::SpanBorderless:
      ok = ApplySpanBorderlessMode();
      break;
    }

    if (!ok && config_.mode != SessionShellWindowMode::Windowed) {
      BootstrapTrace(
          "SplashUi window_mode_fallback requested=" +
          std::string(SessionShellWindowModeToCString(config_.mode)));
      ApplyWindowedMode();
      fell_back = true;
    }

    const char *display_name = placement_.display_id
                                   ? SDL_GetDisplayName(placement_.display_id)
                                   : nullptr;
    std::ostringstream detail;
    detail << "SplashUi window_mode="
           << SessionShellWindowModeToCString(config_.mode) << " display_id="
           << static_cast<unsigned long long>(placement_.display_id)
           << " invalid_display=" << (placement_.invalid_display ? 1 : 0)
           << " fallback=" << (fell_back ? 1 : 0)
           << " bounds=" << placement_.bounds.w << 'x' << placement_.bounds.h
           << std::showpos << placement_.bounds.x << placement_.bounds.y
           << std::noshowpos << " create=" << placement_.width << 'x'
           << placement_.height;
    if (display_name && *display_name)
      detail << " name=\"" << display_name << '"';
    BootstrapTrace(detail.str());
  }

  void ReleaseUiResources(bool keep_video_subsystem,
                          bool keep_native_window = false) {
    ReleaseTextResources();
    if (logo_)
      SDL_DestroyTexture(logo_);
    if (renderer_)
      SDL_DestroyRenderer(renderer_);
    if (window_)
      SDL_DestroyWindow(window_);
#if defined(_WIN32)
    if (native_window_ && !keep_native_window)
      DestroyWindow(native_window_);
#endif
    logo_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
#if defined(_WIN32)
    if (!keep_native_window)
      native_window_ = nullptr;
#endif
    if (owns_sdl_video_ && !keep_video_subsystem)
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    owns_sdl_video_ = false;
  }

  void PresentHandoffFrame() {
    if (!renderer_)
      return;

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
    SDL_SyncWindow(window_);
#if defined(_WIN32)
    FlushDesktopComposition();
#endif
  }

  static SDL_Color Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return SDL_Color{r, g, b, a};
  }

  static float ResponsiveTextSize(float reference, float fraction, float min_px,
                                  float max_px) {
    return std::clamp(reference * fraction, min_px, max_px);
  }

  static float DebugTextScaleForSize(float pixel_size) {
    return pixel_size / static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
  }

  static float DebugTextPixelSize(float pixel_size) {
    return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE *
           DebugTextScaleForSize(pixel_size);
  }

  static float DebugTextLineAdvance(float pixel_size) {
    return (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 4.0f) *
           DebugTextScaleForSize(pixel_size);
  }

  void DrawScaledDebugText(float x, float y, const std::string &text,
                           float pixel_size) {
    const float scale = DebugTextScaleForSize(pixel_size);
    float old_scale_x = 1.0f;
    float old_scale_y = 1.0f;
    SDL_GetRenderScale(renderer_, &old_scale_x, &old_scale_y);
    SDL_SetRenderScale(renderer_, scale, scale);
    SDL_RenderDebugText(renderer_, x / scale, y / scale, text.c_str());
    SDL_SetRenderScale(renderer_, old_scale_x, old_scale_y);
  }

#if USE_SDL3_TTF
  std::vector<fs::path> TextFontCandidates() const {
    std::vector<fs::path> candidates;
    if (!config_.runtime_root.empty()) {
      candidates.push_back(config_.runtime_root / "basew" / "fonts" /
                           "AtkinsonHyperLegible-Regular.otf");
      candidates.push_back(config_.runtime_root / "fonts" /
                           "AtkinsonHyperLegible-Regular.otf");
    }

    if (const char *base_path = SDL_GetBasePath()) {
      fs::path base = Utf8Path(base_path);
      candidates.push_back(base / "basew" / "fonts" /
                           "AtkinsonHyperLegible-Regular.otf");
      candidates.push_back(base / "fonts" / "AtkinsonHyperLegible-Regular.otf");
    }

#if defined(_WIN32)
    WCHAR windows_dir[MAX_PATH];
    UINT windows_dir_len = GetWindowsDirectoryW(windows_dir, MAX_PATH);
    if (windows_dir_len > 0 && windows_dir_len < MAX_PATH) {
      fs::path fonts_dir = fs::path(windows_dir) / L"Fonts";
      candidates.push_back(fonts_dir / L"segoeui.ttf");
      candidates.push_back(fonts_dir / L"arial.ttf");
    }
#elif defined(__APPLE__)
    candidates.push_back("/System/Library/Fonts/Supplemental/Arial.ttf");
    candidates.push_back("/Library/Fonts/Arial.ttf");
#else
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back(
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    candidates.push_back(
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
#endif

    return candidates;
  }

  void InitializeTextRenderer() {
    if (!TTF_Init()) {
      BootstrapTrace(std::string("SplashUi ttf_init_failed error=") +
                     SDL_GetError());
      return;
    }

    owns_ttf_ = true;
    for (const fs::path &candidate : TextFontCandidates()) {
      std::error_code ec;
      if (candidate.empty() || !fs::is_regular_file(candidate, ec))
        continue;
      ttf_font_path_ = candidate;
      BootstrapTrace("SplashUi ttf_font=" + GenericPath(candidate));
      return;
    }

    BootstrapTrace("SplashUi ttf_font_missing using_sdl_debug_text=1");
  }

  TTF_Font *GetTextFont(float pixel_size) {
    if (!owns_ttf_ || ttf_font_path_.empty())
      return nullptr;

    int size = std::max(1, static_cast<int>(std::round(pixel_size)));
    auto it = ttf_fonts_.find(size);
    if (it != ttf_fonts_.end())
      return it->second;

    TTF_Font *font = TTF_OpenFont(GenericPath(ttf_font_path_).c_str(),
                                  static_cast<float>(size));
    if (!font) {
      BootstrapTrace("SplashUi ttf_open_failed size=" + std::to_string(size) +
                     " error=" + SDL_GetError());
      return nullptr;
    }
    ttf_fonts_[size] = font;
    return font;
  }

  void ReleaseTextResources() {
    for (auto &entry : ttf_fonts_) {
      if (entry.second)
        TTF_CloseFont(entry.second);
    }
    ttf_fonts_.clear();
    ttf_font_path_.clear();

    if (owns_ttf_) {
      TTF_Quit();
      owns_ttf_ = false;
    }
  }
#else
  void InitializeTextRenderer() {}
  void ReleaseTextResources() {}
#endif

  float TextLineAdvance(float pixel_size) {
#if USE_SDL3_TTF
    if (TTF_Font *font = GetTextFont(pixel_size)) {
      return std::max(static_cast<float>(TTF_GetFontLineSkip(font)),
                      std::max(static_cast<float>(TTF_GetFontHeight(font)),
                               pixel_size * 1.25f));
    }
#endif
    return DebugTextLineAdvance(pixel_size);
  }

  float MeasureTextLineWidth(const std::string &text, float pixel_size) {
    if (text.empty())
      return 0.0f;

#if USE_SDL3_TTF
    if (TTF_Font *font = GetTextFont(pixel_size)) {
      int w = 0;
      int h = 0;
      if (TTF_GetStringSize(font, text.c_str(), text.size(), &w, &h))
        return static_cast<float>(w);
    }
#endif

    return static_cast<float>(text.size()) * DebugTextPixelSize(pixel_size);
  }

  std::vector<std::string> WrapTextByWidth(const std::string &text,
                                           float pixel_size, float max_width) {
    std::vector<std::string> lines;
    std::stringstream paragraphs(text);
    std::string paragraph;

    while (std::getline(paragraphs, paragraph)) {
      std::istringstream words(paragraph);
      std::string word;
      std::string current;

      while (words >> word) {
        std::string candidate = current.empty() ? word : current + " " + word;
        if (current.empty() ||
            MeasureTextLineWidth(candidate, pixel_size) <= max_width) {
          current = candidate;
          continue;
        }

        lines.push_back(current);
        current = word;
      }

      lines.push_back(current);
    }

    if (lines.empty())
      lines.push_back({});

    return lines;
  }

  float MeasureTextBlockHeight(const std::string &text, float pixel_size,
                               float max_width) {
    return static_cast<float>(
               WrapTextByWidth(text, pixel_size, max_width).size()) *
           TextLineAdvance(pixel_size);
  }

  void DrawTextLine(float x, float y, const std::string &line, float pixel_size,
                    SDL_Color color) {
    if (line.empty())
      return;

#if USE_SDL3_TTF
    if (TTF_Font *font = GetTextFont(pixel_size)) {
      SDL_Surface *surface =
          TTF_RenderText_Blended(font, line.c_str(), line.size(), color);
      if (surface) {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
        if (texture) {
          SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
          SDL_FRect dst{x, y, static_cast<float>(surface->w),
                        static_cast<float>(surface->h)};
          SDL_RenderTexture(renderer_, texture, nullptr, &dst);
          SDL_DestroyTexture(texture);
        }
        SDL_DestroySurface(surface);
        return;
      }
    }
#endif

    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    DrawScaledDebugText(x, y, line, pixel_size);
  }

  void DrawCenteredTextBlock(float center_x, float y, const std::string &text,
                             float pixel_size, float max_width,
                             SDL_Color color) {
    float line_y = y;
    const float line_advance = TextLineAdvance(pixel_size);
    for (const std::string &line :
         WrapTextByWidth(text, pixel_size, max_width)) {
      const float line_width = MeasureTextLineWidth(line, pixel_size);
      DrawTextLine(center_x - (line_width * 0.5f), line_y, line, pixel_size,
                   color);
      line_y += line_advance;
    }
  }

  float DrawCenteredTextMeasured(float center_x, float y,
                                 const std::string &text, float pixel_size,
                                 float max_width, SDL_Color color) {
    float line_y = y;
    const float line_advance = TextLineAdvance(pixel_size);
    for (const std::string &line :
         WrapTextByWidth(text, pixel_size, max_width)) {
      const float line_width = MeasureTextLineWidth(line, pixel_size);
      DrawTextLine(center_x - (line_width * 0.5f), line_y, line, pixel_size,
                   color);
      line_y += line_advance;
    }
    return line_y;
  }

  void DrawButton(const SDL_FRect &rect, const std::string &label,
                  float text_size, bool primary) {
    SDL_SetRenderDrawColor(renderer_, primary ? 159 : 56, primary ? 202 : 64,
                           primary ? 68 : 56, 255);
    SDL_RenderFillRect(renderer_, &rect);
    SDL_SetRenderDrawColor(renderer_, 229, 235, 219, 255);
    SDL_RenderRect(renderer_, &rect);
    const float text_width = MeasureTextLineWidth(label, text_size);
    const float text_height = TextLineAdvance(text_size);
    DrawTextLine(rect.x + (rect.w - text_width) * 0.5f,
                 rect.y + (rect.h - text_height) * 0.5f, label, text_size,
                 Color(229, 235, 219));
  }

  void AnimateProgress() {
    if (progress_total_ <= 0)
      return;

    const uint64_t now = SDL_GetTicks();
    if (last_progress_tick_ == 0) {
      last_progress_tick_ = now;
      return;
    }

    const float dt = static_cast<float>(now - last_progress_tick_) / 1000.0f;
    last_progress_tick_ = now;
    if (dt <= 0.0f)
      return;

    const float step = std::min(1.0f, dt * 2.5f);
    progress_display_ += (progress_target_ - progress_display_) * step;
    if (std::fabs(progress_target_ - progress_display_) < 0.0025f)
      progress_display_ = progress_target_;
  }

  void DrawArc(float cx, float cy, float radius, float start, float end,
               int segments) {
    if (segments < 1)
      return;
    float last_x = cx + std::cos(start) * radius;
    float last_y = cy + std::sin(start) * radius;
    for (int i = 1; i <= segments; ++i) {
      const float t = start + (end - start) * (static_cast<float>(i) /
                                               static_cast<float>(segments));
      const float x = cx + std::cos(t) * radius;
      const float y = cy + std::sin(t) * radius;
      SDL_RenderLine(renderer_, last_x, last_y, x, y);
      last_x = x;
      last_y = y;
    }
  }

  void DrawCircularIndicator(float cx, float cy, float radius) {
    constexpr float tau = 6.28318530718f;
    const int ring_segments = std::max(40, static_cast<int>(radius * 1.8f));

    SDL_SetRenderDrawColor(renderer_, 44, 50, 52, 255);
    for (int i = 0; i < 4; ++i)
      DrawArc(cx, cy, radius - static_cast<float>(i), 0.0f, tau, ring_segments);

    if (progress_total_ > 0) {
      const float progress = std::clamp(progress_display_, 0.0f, 1.0f);
      const float start = -1.57079632679f;
      const float end = start + tau * progress;
      SDL_SetRenderDrawColor(renderer_, 155, 198, 66, 255);
      for (int i = 0; i < 5; ++i)
        DrawArc(cx, cy, radius - static_cast<float>(i), start, end,
                std::max(1, static_cast<int>(ring_segments * progress)));

      const int percent = static_cast<int>(std::round(progress * 100.0f));
      const std::string value = std::to_string(percent) + "%";
      const float value_size = std::clamp(radius * 0.42f, 12.0f, 18.0f);
      const float value_width = MeasureTextLineWidth(value, value_size);
      DrawTextLine(cx - value_width * 0.5f,
                   cy - TextLineAdvance(value_size) * 0.5f, value, value_size,
                   Color(229, 235, 219));
    } else {
      const float tick = static_cast<float>(SDL_GetTicks() % 1200u) / 1200.0f;
      const float start = -1.57079632679f + tau * tick;
      const float end = start + 1.35f;
      SDL_SetRenderDrawColor(renderer_, 155, 198, 66, 255);
      for (int i = 0; i < 5; ++i)
        DrawArc(cx, cy, radius - static_cast<float>(i), start, end,
                ring_segments / 4);
    }
  }

  void Render() {
    if (closed_ || !renderer_)
      return;

    int width = 0;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &width, &height);

    SDL_SetRenderDrawColor(renderer_, 14, 16, 18, 255);
    SDL_RenderClear(renderer_);

    const float center_x = static_cast<float>(width) * 0.5f;
    const float size_reference = static_cast<float>(std::min(width, height));
    const float headline_text_size = ResponsiveTextSize(
        size_reference, 0.026f, kHeadlineTextMinPx, kHeadlineTextMaxPx);
    const float detail_text_size = ResponsiveTextSize(
        size_reference, 0.018f, kDetailTextMinPx, kDetailTextMaxPx);
    const float progress_text_size = ResponsiveTextSize(
        size_reference, 0.016f, kProgressTextMinPx, kProgressTextMaxPx);
    const float button_text_size = ResponsiveTextSize(
        size_reference, 0.017f, kButtonTextMinPx, kButtonTextMaxPx);
    const float legal_text_size = ResponsiveTextSize(
        size_reference, 0.0125f, kLegalTextMinPx, kLegalTextMaxPx);
    const float virtual_43_width = std::min(
        static_cast<float>(width), static_cast<float>(height) * (4.0f / 3.0f));
    const float legal_margin = 12.0f;
    const float legal_width =
        std::min<float>(virtual_43_width, static_cast<float>(width) - 96.0f);
    const float legal_height =
        MeasureTextBlockHeight(kLegalText, legal_text_size, legal_width);
    const float footer_top =
        static_cast<float>(height) - legal_height - legal_margin;
    const float content_top = 24.0f;
    const float content_bottom = footer_top - 22.0f;
    const float available_height = std::max(0.0f, content_bottom - content_top);
    const float status_width =
        std::min<float>(virtual_43_width, static_cast<float>(width) - 140.0f);

    const bool has_progress = !progress_label_.empty();
    const bool has_headline = !headline_.empty();
    const bool has_detail = !detail_.empty();
    const float progress_height =
        has_progress ? MeasureTextBlockHeight(progress_label_,
                                              progress_text_size, status_width)
                     : 0.0f;
    const float headline_height =
        has_headline ? MeasureTextBlockHeight(headline_, headline_text_size,
                                              status_width)
                     : 0.0f;
    const float detail_height =
        has_detail
            ? MeasureTextBlockHeight(detail_, detail_text_size, status_width)
            : 0.0f;

    float text_block_height = 0.0f;
    if (has_progress)
      text_block_height += progress_height;
    if (has_headline) {
      if (text_block_height > 0.0f)
        text_block_height += 4.0f;
      text_block_height += headline_height;
    }
    if (has_detail) {
      if (text_block_height > 0.0f)
        text_block_height += 4.0f;
      text_block_height += detail_height;
    }

    float spinner_radius = std::clamp(
        static_cast<float>(std::min(width, height)) * 0.055f, 26.0f, 46.0f);
    float spinner_block_height = spinner_radius * 2.0f;
    const float button_block_height =
        prompt_active_ ? (18.0f + kButtonHeight) : 0.0f;
    const float gap_logo_spinner = 20.0f;
    const float gap_spinner_text = 18.0f;
    const float gap_text_buttons = prompt_active_ ? 18.0f : 0.0f;
    const float minimum_banner_height = 96.0f;
    const float minimum_spinner_block = 48.0f;
    const float minimum_stack_height =
        minimum_banner_height + minimum_spinner_block + text_block_height +
        button_block_height + gap_logo_spinner + gap_spinner_text +
        gap_text_buttons;
    if (minimum_stack_height > available_height &&
        spinner_block_height > minimum_spinner_block) {
      const float overflow = minimum_stack_height - available_height;
      const float shrink =
          std::min((spinner_block_height - minimum_spinner_block) * 0.5f,
                   overflow * 0.5f);
      spinner_radius = std::max(24.0f, spinner_radius - shrink);
      spinner_block_height = spinner_radius * 2.0f;
    }

    const float banner_aspect =
        static_cast<float>(generated::kBootstrapLogoHeight) /
        static_cast<float>(generated::kBootstrapLogoWidth);
    float banner_width = virtual_43_width;
    float banner_height = banner_width * banner_aspect;
    const float max_banner_height =
        std::max(minimum_banner_height,
                 available_height - spinner_block_height - text_block_height -
                     button_block_height - gap_logo_spinner - gap_spinner_text -
                     gap_text_buttons);
    const float preferred_banner_height =
        std::min(max_banner_height, static_cast<float>(height) * 0.34f);
    if (banner_height > preferred_banner_height) {
      banner_height = preferred_banner_height;
      banner_width = banner_height / banner_aspect;
    }
    if (banner_height > max_banner_height) {
      banner_height = max_banner_height;
      banner_width = banner_height / banner_aspect;
    }
    if (banner_width > virtual_43_width) {
      banner_width = virtual_43_width;
      banner_height = banner_width * banner_aspect;
    }

    const float stack_height = banner_height + spinner_block_height +
                               text_block_height + button_block_height +
                               gap_logo_spinner + gap_spinner_text +
                               gap_text_buttons;
    const float stack_top =
        content_top + std::max(0.0f, (available_height - stack_height) * 0.35f);
    SDL_FRect banner_rect{center_x - banner_width * 0.5f, stack_top,
                          banner_width, banner_height};
    SDL_RenderTexture(renderer_, logo_, nullptr, &banner_rect);

    const float spinner_y =
        banner_rect.y + banner_rect.h + gap_logo_spinner + spinner_radius;
    DrawCircularIndicator(center_x, spinner_y, spinner_radius);

    float text_y = spinner_y + spinner_radius + gap_spinner_text;
    if (has_progress) {
      text_y = DrawCenteredTextMeasured(center_x, text_y, progress_label_,
                                        progress_text_size, status_width,
                                        Color(155, 198, 66));
      text_y += 4.0f;
    }
    if (has_headline) {
      text_y = DrawCenteredTextMeasured(center_x, text_y, headline_,
                                        headline_text_size, status_width,
                                        Color(229, 235, 219));
    }
    if (has_detail) {
      if (has_headline || has_progress)
        text_y += 4.0f;
      text_y =
          DrawCenteredTextMeasured(center_x, text_y, detail_, detail_text_size,
                                   status_width, Color(170, 179, 163));
    }

    if (prompt_active_) {
      const float total_button_width = (kButtonWidth * 2.0f) + kButtonGap;
      const float button_y = text_y + gap_text_buttons;
      install_button_ = SDL_FRect{center_x - total_button_width * 0.5f,
                                  button_y, kButtonWidth, kButtonHeight};
      exit_button_ = SDL_FRect{install_button_.x + kButtonWidth + kButtonGap,
                               button_y, kButtonWidth, kButtonHeight};
      DrawButton(install_button_, "Install", button_text_size, true);
      DrawButton(exit_button_, "Exit", button_text_size, false);
    }

    DrawCenteredTextBlock(center_x, footer_top, kLegalText, legal_text_size,
                          legal_width, Color(160, 170, 150));

    SDL_RenderPresent(renderer_);
  }

  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *logo_ = nullptr;
#if defined(_WIN32)
  HWND native_window_ = nullptr;
  bool wrapped_external_window_ = false;
#endif
  SessionShellWindowConfig config_{};
  SessionShellWindowPlacement placement_{};
  bool owns_sdl_video_ = false;
  bool closed_ = false;
  bool prompt_active_ = false;
  uint64_t created_at_ = SDL_GetTicks();
  std::optional<bool> prompt_result_;
  std::string headline_;
  std::string detail_;
  std::string progress_label_ = "Starting";
  uint64_t progress_current_ = 0;
  uint64_t progress_total_ = 0;
  float progress_display_ = 0.0f;
  float progress_target_ = 0.0f;
  uint64_t last_progress_tick_ = 0;
  SDL_FRect install_button_{};
  SDL_FRect exit_button_{};
#if USE_SDL3_TTF
  bool owns_ttf_ = false;
  fs::path ttf_font_path_;
  std::map<int, TTF_Font *> ttf_fonts_;
#endif
};

class UiHandle {
public:
  UiHandle(Role role, bool quiet_status,
           const SessionShellWindowConfig *session_shell_config = nullptr) {
    if (quiet_status) {
      ui_ = std::make_unique<SilentUi>();
      return;
    }
    if (role == Role::Client) {
      try {
        ui_ = std::make_unique<SplashUi>(session_shell_config
                                             ? *session_shell_config
                                             : SessionShellWindowConfig{});
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

struct CurlStringContext {
  std::string *output = nullptr;
  size_t maximum_size = 0;
  bool overflow = false;
};

struct CurlFileContext {
  std::ofstream *file = nullptr;
  uint64_t expected_size = 0;
  uint64_t bytes_written = 0;
  bool overflow = false;
  bool write_error = false;
};

struct CurlProgressContext {
  BootstrapUi *ui = nullptr;
  std::string label;
};

size_t CurlWriteToString(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *context = static_cast<CurlStringContext *>(userdata);
  if (size != 0 && nmemb > std::numeric_limits<size_t>::max() / size) {
    context->overflow = true;
    return 0;
  }
  const size_t bytes = size * nmemb;
  if (bytes > context->maximum_size -
                  std::min(context->output->size(), context->maximum_size)) {
    context->overflow = true;
    return 0;
  }
  context->output->append(ptr, bytes);
  return bytes;
}

size_t CurlWriteToFile(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *context = static_cast<CurlFileContext *>(userdata);
  if (size != 0 && nmemb > std::numeric_limits<size_t>::max() / size) {
    context->overflow = true;
    return 0;
  }
  const size_t bytes = size * nmemb;
  if (bytes > context->expected_size -
                  std::min(context->bytes_written, context->expected_size)) {
    context->overflow = true;
    return 0;
  }
  if (bytes > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
    context->write_error = true;
    return 0;
  }
  context->file->write(ptr, static_cast<std::streamsize>(bytes));
  if (!context->file->good()) {
    context->write_error = true;
    return 0;
  }
  context->bytes_written += static_cast<uint64_t>(bytes);
  return bytes;
}

int CurlProgressThunk(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                      curl_off_t, curl_off_t) {
  auto *ctx = static_cast<CurlProgressContext *>(userdata);
  if (ctx->ui) {
    const uint64_t downloaded = dlnow > 0 ? static_cast<uint64_t>(dlnow) : 0;
    const uint64_t total = dltotal > 0 ? static_cast<uint64_t>(dltotal) : 0;
    if (!ctx->ui->SetProgress(ctx->label, downloaded, total))
      return 1;
    if (!ctx->ui->Pump())
      return 1;
  }
  return 0;
}

void RestrictCurlToHttps(CURL *curl) {
#if LIBCURL_VERSION_NUM >= 0x075500
  const CURLcode protocols =
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
  const CURLcode redirect_protocols =
      curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
  const CURLcode protocols = curl_easy_setopt(
      curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
  const CURLcode redirect_protocols = curl_easy_setopt(
      curl, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
  if (protocols != CURLE_OK || redirect_protocols != CURLE_OK)
    throw std::runtime_error(
        "libcurl could not enforce the updater's HTTPS-only transport policy");
}

int RemainingBudgetMs(const clock_type::time_point &deadline) {
  const auto now = clock_type::now();
  if (now >= deadline)
    return 0;
  return static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
          .count());
}

std::string HttpGetString(const std::string &url, int timeout_ms,
                          BootstrapUi *ui, const std::string &label,
                          size_t maximum_size = kMaxHttpMetadataBytes) {
  CURL *curl = curl_easy_init();
  if (!curl)
    throw std::runtime_error("curl_easy_init failed");

  std::string response;
  CurlStringContext response_context{&response, maximum_size, false};
  CurlProgressContext progress{ui, label};
  curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
  headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  RestrictCurlToHttps(curl);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_context);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressThunk);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

  const CURLcode result = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  if (response_context.overflow)
    throw std::runtime_error(label + " exceeds the maximum supported size");
  if (result != CURLE_OK)
    throw std::runtime_error(curl_easy_strerror(result));
  return response;
}

void HttpDownloadFile(const std::string &url, const fs::path &path,
                      uint64_t expected_size, int timeout_ms, BootstrapUi *ui,
                      const std::string &label) {
  if (expected_size == 0 || expected_size > kMaxUpdatePackageBytes ||
      expected_size >
          static_cast<uint64_t>(std::numeric_limits<curl_off_t>::max()))
    throw std::runtime_error(
        "Update package size is outside the supported range");
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open())
    throw std::runtime_error("Could not open " + GenericPath(path));

  CURL *curl = curl_easy_init();
  if (!curl) {
    file.close();
    std::error_code ignored;
    fs::remove(path, ignored);
    throw std::runtime_error("curl_easy_init failed");
  }

  CurlProgressContext progress{ui, label};
  CurlFileContext output{&file, expected_size, 0, false, false};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  RestrictCurlToHttps(curl);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE,
                   static_cast<curl_off_t>(expected_size));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToFile);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressThunk);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

  const CURLcode result = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  file.flush();
  const bool close_failed = !file.good();
  file.close();

  if (result != CURLE_OK || output.overflow || output.write_error ||
      close_failed || output.bytes_written != expected_size) {
    std::error_code ignored;
    fs::remove(path, ignored);
    if (output.overflow)
      throw std::runtime_error(
          "Downloaded update package exceeds its authenticated size");
    if (output.write_error || close_failed)
      throw std::runtime_error("Could not write the downloaded update package");
    if (result == CURLE_OK)
      throw std::runtime_error("Downloaded update package size mismatch");
    throw std::runtime_error(curl_easy_strerror(result));
  }
}

Json::Value ParseJsonString(const std::string &text,
                            const std::string &context) {
  Json::CharReaderBuilder builder;
  builder["allowComments"] = false;
  builder["allowTrailingCommas"] = false;
  builder["collectComments"] = false;
  builder["failIfExtra"] = true;
  builder["rejectDupKeys"] = true;
  builder["strictRoot"] = true;
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

std::optional<ReleaseAsset>
FindAssetByName(const std::vector<ReleaseAsset> &assets,
                const std::string &name) {
  std::optional<ReleaseAsset> match;
  for (const ReleaseAsset &asset : assets) {
    if (asset.name != name)
      continue;
    if (match)
      throw std::runtime_error("Release contains duplicate assets named " +
                               name);
    match = asset;
  }
  return match;
}

AssetIntegrity SignedTargetAssetIntegrity(const Json::Value &target,
                                          const std::string &name,
                                          std::string_view kind,
                                          std::string_view role) {
  const Json::Value &files = target["files"];
  if (!files.isArray())
    throw std::runtime_error(
        "Signed release target does not contain artifact integrity metadata");

  std::optional<AssetIntegrity> match;
  for (const Json::Value &file : files) {
    if (!file.isObject() || JsonString(file, "name") != name)
      continue;
    if (match)
      throw std::runtime_error("Signed release target repeats artifact " +
                               name);
    if (JsonString(file, "kind") != kind || JsonString(file, "role") != role)
      throw std::runtime_error(
          "Signed release target has the wrong contract for artifact " + name);
    AssetIntegrity integrity;
    integrity.sha256 = JsonString(file, "sha256");
    integrity.size = JsonUInt64(file, "size", 0);
    if (!IsSha256Hex(integrity.sha256) || integrity.size == 0)
      throw std::runtime_error(
          "Signed release target has invalid integrity metadata for artifact " +
          name);
    match = std::move(integrity);
  }
  if (!match)
    throw std::runtime_error(
        "Signed release target does not authenticate artifact " + name);
  return *match;
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

std::vector<Json::Value> SelectReleases(const Json::Value &releases,
                                        const UpdaterConfig &config) {
  if (!releases.isArray())
    throw std::runtime_error("GitHub releases payload was not an array");

  std::vector<Json::Value> matches;
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

    matches.push_back(release);
  }

  if (matches.empty())
    throw std::runtime_error("No matching GitHub release found for channel " +
                             config.channel);
  return matches;
}

FileEntry FileEntryFromJson(const Json::Value &value) {
  FileEntry entry;
  entry.path = JsonString(value, "path");
  entry.sha256 = JsonString(value, "sha256");
  entry.size = JsonUInt64(value, "size", 0);
  const uint64_t mode = JsonUInt64(value, "mode", 0);
  if (mode == 0644 || mode == 0755)
    entry.mode = static_cast<uint32_t>(mode);
  if (!entry.path.empty())
    entry.path =
        RequireSafeUpdaterPath(entry.path, "remote manifest file path");
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

void ValidateSignedManifestTargetIdentity(const Json::Value &manifest,
                                          const Json::Value &release_index,
                                          const Json::Value &target) {
  const Json::Value &platform = manifest["platform"];
  if (!platform.isObject() ||
      JsonString(platform, "id") != JsonString(target, "platform_id") ||
      JsonString(platform, "os") != JsonString(target, "os") ||
      JsonString(platform, "arch") != JsonString(target, "arch") ||
      JsonString(platform, "id") != CurrentPlatformId())
    throw std::runtime_error(
        "Update manifest platform does not match the signed release target");

  const Json::Value &build = manifest["build"];
  const std::string signed_commit = JsonString(release_index, "commit_sha");
  if (!build.isObject() || signed_commit.empty() ||
      JsonString(build, "commit_sha") != signed_commit)
    throw std::runtime_error(
        "Update manifest build does not match the signed release index");
}

RemotePayload
DiscoverRemotePayload(const UpdaterConfig &config, Role role, BootstrapUi *ui,
                      const clock_type::time_point &deadline,
                      const InstallManifest &installed,
                      std::map<std::string, uint64_t> *observed_revocations) {
  Json::Value releases_json(Json::arrayValue);
  for (int page = 1; page <= kMaxReleaseFeedPages; ++page) {
    const int releases_timeout = RemainingBudgetMs(deadline);
    if (releases_timeout <= 0)
      break;
    const std::string releases_url =
        "https://api.github.com/repos/" + config.repo +
        "/releases?per_page=" + std::to_string(kReleaseFeedPageSize) +
        "&page=" + std::to_string(page);
    const Json::Value release_page =
        ParseJsonString(HttpGetString(releases_url, releases_timeout, ui,
                                      "Checking release feed"),
                        "release feed page");
    if (!release_page.isArray())
      throw std::runtime_error("GitHub releases payload was not an array");
    for (const Json::Value &release : release_page)
      releases_json.append(release);
    if (release_page.size() < kReleaseFeedPageSize)
      break;
  }
  const std::vector<Json::Value> matching_releases =
      SelectReleases(releases_json, config);
  std::string last_rejection;
  for (const Json::Value &release_json : matching_releases) {
    if (RemainingBudgetMs(deadline) <= 0)
      break;
    try {
      const std::vector<ReleaseAsset> release_assets =
          ReleaseAssetsFromJson(release_json);

      const auto index_asset =
          FindAssetByName(release_assets, config.release_index_asset);
      if (!index_asset)
        throw std::runtime_error("Release is missing asset " +
                                 config.release_index_asset);

      const int index_timeout = std::max(1, RemainingBudgetMs(deadline));
      const std::string raw_index = HttpGetString(
          index_asset->url, index_timeout, ui, "Loading release index");
      Json::Value index_json;
      std::vector<std::string> verified_key_ids;
      std::string raw_signature;
      bool unsigned_development_index = false;
      if (config.allow_unsigned_development_index) {
        index_json =
            ParseJsonString(raw_index, "unsigned development release index");
        unsigned_development_index = true;
      } else {
        if (!config.release_trust_policy)
          throw std::runtime_error(
              "Release update checks are disabled because this install has no "
              "signed-release trust policy");
        const std::string signature_name =
            RequireSafeUpdaterFileName(config.release_index_asset + ".sig.json",
                                       "release index signature asset name");
        const auto signature_asset =
            FindAssetByName(release_assets, signature_name);
        if (!signature_asset)
          throw std::runtime_error("Release is missing signature asset " +
                                   signature_name);
        const int signature_timeout = std::max(1, RemainingBudgetMs(deadline));
        raw_signature = HttpGetString(signature_asset->url, signature_timeout,
                                      ui, "Loading release index signature",
                                      kMaxSignatureEnvelopeBytes);
        VerifiedReleaseIndex verified = VerifyReleaseIndexSignature(
            raw_index, config.release_index_asset, raw_signature,
            *config.release_trust_policy, config.repo, config.channel);
        index_json = std::move(verified.index);
        verified_key_ids = std::move(verified.verified_key_ids);
      }

      const std::string remote_version = JsonString(index_json, "version");
      const std::string release_tag = JsonString(release_json, "tag_name");
      if (JsonUInt64(index_json, "schema_version", 0) != 3 ||
          remote_version.empty() ||
          JsonString(index_json, "tag") != release_tag ||
          JsonString(index_json, "repo") != config.repo ||
          JsonString(index_json, "channel") != config.channel)
        throw std::runtime_error(
            "Release index identity does not match the selected release");
      const ReleaseIndexSecurityMetadata security =
          ValidateReleaseIndexSecurityMetadata(index_json);
      const std::string release_index_sha256 =
          Sha256Hex(reinterpret_cast<const uint8_t *>(raw_index.data()),
                    raw_index.size());
      if (observed_revocations) {
        const auto revocation =
            observed_revocations->find(release_index_sha256);
        if (revocation != observed_revocations->end() &&
            revocation->second >= security.sequence)
          throw std::runtime_error(
              "Release index was revoked by a newer authenticated candidate");
      }
      if (std::find(security.revoked_index_sha256s.begin(),
                    security.revoked_index_sha256s.end(),
                    release_index_sha256) !=
          security.revoked_index_sha256s.end())
        throw std::runtime_error("Release index revokes itself");
      if (observed_revocations) {
        for (const std::string &revoked_sha256 :
             security.revoked_index_sha256s) {
          uint64_t &revoking_sequence = (*observed_revocations)[revoked_sha256];
          revoking_sequence = std::max(revoking_sequence, security.sequence);
        }
      }
      const Json::Value targets = index_json["targets"];
      if (!targets.isArray())
        throw std::runtime_error("Release index targets are not an array");
      Json::Value selected_target;
      for (const Json::Value &target : targets) {
        if (JsonString(target, "platform_id") == CurrentPlatformId()) {
          if (!selected_target.isNull())
            throw std::runtime_error(
                "Release index contains duplicate targets for " +
                CurrentPlatformId());
          selected_target = target;
        }
      }
      if (selected_target.isNull())
        throw std::runtime_error(
            "Release index does not contain a target for " +
            CurrentPlatformId());

      const std::string role_name = RoleToCString(role);
      const Json::Value roles = selected_target["roles"];
      if (!roles.isObject() || !roles.isMember(role_name))
        throw std::runtime_error(
            "Release index does not contain role metadata for " + role_name);
      const Json::Value role_json = roles[role_name];

      const std::string manifest_name = RequireSafeUpdaterFileName(
          JsonString(role_json, "update_manifest_name"),
          "update manifest asset name");
      const std::string package_name = RequireSafeUpdaterFileName(
          JsonString(role_json, "update_package_name"),
          "update package asset name");
      const AssetIntegrity manifest_integrity = SignedTargetAssetIntegrity(
          selected_target, manifest_name, "manifest", role_name);
      const AssetIntegrity package_integrity = SignedTargetAssetIntegrity(
          selected_target, package_name, "archive", role_name);
      const auto manifest_asset =
          FindAssetByName(release_assets, manifest_name);
      const auto package_asset = FindAssetByName(release_assets, package_name);
      if (!manifest_asset)
        throw std::runtime_error("Release is missing asset " + manifest_name);
      if (!package_asset)
        throw std::runtime_error("Release is missing asset " + package_name);

      const int manifest_timeout = std::max(1, RemainingBudgetMs(deadline));
      const std::string raw_manifest = HttpGetString(
          manifest_asset->url, manifest_timeout, ui, "Loading update manifest");
      if (raw_manifest.size() != manifest_integrity.size ||
          Sha256Hex(reinterpret_cast<const uint8_t *>(raw_manifest.data()),
                    raw_manifest.size()) != manifest_integrity.sha256)
        throw std::runtime_error(
            "Update manifest does not match the signed release index");
      const Json::Value manifest_json =
          ParseJsonString(raw_manifest, "update manifest");
      ValidateSignedManifestTargetIdentity(manifest_json, index_json,
                                           selected_target);

      RemotePayload payload;
      payload.version = remote_version;
      payload.tag = release_tag;
      payload.role = role_name;
      payload.launch_exe = RequireSafeUpdaterPath(
          JsonStringAny(role_json, {"launch_exe", "launcher_exe"},
                        DefaultLaunchRelpath(role)),
          "launch path");
      payload.engine_library = RequireSafeUpdaterPath(
          JsonStringAny(role_json, {"engine_library", "runtime_exe"},
                        DefaultEngineLibraryRelpath(role)),
          "engine path");
      payload.update_manifest_name = manifest_name;
      payload.update_package_name = package_name;
      payload.local_manifest_name = RequireSafeUpdaterFileName(
          JsonString(role_json, "local_manifest_name", kLocalManifestName),
          "local manifest name");
      payload.manifest_url = manifest_asset->url;
      payload.package_url = package_asset->url;
      payload.release_index_name = config.release_index_asset;
      payload.release_index_sha256 = release_index_sha256;
      payload.raw_release_index = raw_index;
      payload.raw_release_signature = std::move(raw_signature);
      payload.raw_update_manifest = raw_manifest;
      payload.verified_release_key_ids = std::move(verified_key_ids);
      payload.revoked_release_index_sha256s = security.revoked_index_sha256s;
      payload.release_expires_at_utc = security.expires_at_utc;
      payload.release_sequence = security.sequence;
      payload.unsigned_development_index = unsigned_development_index;
      payload.update_manifest_integrity = manifest_integrity;
      payload.update_package_integrity = package_integrity;
      payload.manifest_json = manifest_json;
      ValidateRemotePayloadManifest(payload);
      ValidateReleaseSequenceAgainstInstalled(payload, installed);
      return payload;
    } catch (const std::exception &error) {
      const std::string tag = JsonString(release_json, "tag_name", "<unknown>");
      last_rejection = tag + ": " + error.what();
      BootstrapTrace("Release candidate rejected: " + last_rejection);
    }
  }
  if (last_rejection.empty())
    throw std::runtime_error("Release discovery budget expired before a "
                             "candidate could be verified");
  throw std::runtime_error(
      "No release candidate is verifiable by this install; last rejection: " +
      last_rejection);
}

struct ZipReadContext {
  std::ifstream *stream = nullptr;
};

size_t ReadZipBytes(void *opaque, mz_uint64 offset, void *buffer,
                    size_t bytes) {
  auto *context = static_cast<ZipReadContext *>(opaque);
  if (!context || !context->stream ||
      offset > static_cast<mz_uint64>(
                   std::numeric_limits<std::streamoff>::max()) ||
      bytes > static_cast<size_t>(
                  std::numeric_limits<std::streamsize>::max()))
    return 0;
  context->stream->clear();
  context->stream->seekg(static_cast<std::streamoff>(offset));
  if (!context->stream->good())
    return 0;
  context->stream->read(static_cast<char *>(buffer),
                        static_cast<std::streamsize>(bytes));
  return static_cast<size_t>(context->stream->gcount());
}

struct ZipWriteContext {
  std::ofstream *stream = nullptr;
  mz_uint64 next_offset = 0;
};

size_t WriteZipBytes(void *opaque, mz_uint64 offset, const void *buffer,
                     size_t bytes) {
  auto *context = static_cast<ZipWriteContext *>(opaque);
  if (!context || !context->stream || offset != context->next_offset ||
      bytes > static_cast<size_t>(
                  std::numeric_limits<std::streamsize>::max()))
    return 0;
  context->stream->write(static_cast<const char *>(buffer),
                         static_cast<std::streamsize>(bytes));
  if (!context->stream->good())
    return 0;
  context->next_offset += static_cast<mz_uint64>(bytes);
  return bytes;
}

void ExtractZip(const fs::path &archive_path, const fs::path &stage_dir,
                const RemotePayload &payload, BootstrapUi *ui) {
  const uint64_t archive_size = fs::file_size(archive_path);
  std::ifstream archive_stream(archive_path, std::ios::binary);
  if (!archive_stream.is_open())
    throw std::runtime_error("Could not open zip " + GenericPath(archive_path));
  ZipReadContext read_context{&archive_stream};
  mz_zip_archive zip{};
  zip.m_pRead = ReadZipBytes;
  zip.m_pIO_opaque = &read_context;
  if (!mz_zip_reader_init(&zip, archive_size, 0))
    throw std::runtime_error("Could not open zip " + GenericPath(archive_path));
  try {
    struct ValidatedZipEntry {
      mz_uint index = 0;
      std::string path;
      bool directory = false;
      uint16_t version_made_by = 0;
      uint32_t external_attributes = 0;
    };

    std::map<std::string, FileEntry> expected_files;
    std::set<std::string> expected_directories;
    for (const FileEntry &file : ManifestFiles(payload.manifest_json)) {
      const std::string key = UpdaterPathCollisionKey(file.path);
      expected_files.emplace(key, file);
      std::string parent = CanonicalUpdaterRelativePath(file.path);
      while (true) {
        const size_t separator = parent.rfind('/');
        if (separator == std::string::npos)
          break;
        parent.resize(separator);
        expected_directories.insert(UpdaterPathCollisionKey(parent));
      }
    }
    const std::string local_manifest_key =
        UpdaterPathCollisionKey(payload.local_manifest_name);
    const mz_uint file_count = mz_zip_reader_get_num_files(&zip);
    const uint64_t maximum_entries =
        static_cast<uint64_t>(expected_files.size()) +
        static_cast<uint64_t>(expected_directories.size()) + 1;
    if (file_count == 0 || static_cast<uint64_t>(file_count) > maximum_entries)
      throw std::runtime_error(
          "Update archive contains an unexpected number of entries");

    std::vector<ValidatedZipEntry> entries;
    entries.reserve(file_count);
    std::map<std::string, std::string> seen_member_paths;
    std::set<std::string> observed_managed_files;
    bool observed_local_manifest = false;
    uint64_t expanded_size = 0;
    for (mz_uint i = 0; i < file_count; ++i) {
      mz_zip_archive_file_stat stat{};
      if (!mz_zip_reader_file_stat(&zip, i, &stat))
        throw std::runtime_error("Could not read an update archive entry");

      const mz_uint filename_size =
          mz_zip_reader_get_filename(&zip, i, nullptr, 0);
      if (filename_size <= 1)
        throw std::runtime_error("Could not read an update archive entry name");
      std::vector<char> filename(filename_size);
      if (mz_zip_reader_get_filename(&zip, i, filename.data(), filename_size) !=
          filename_size)
        throw std::runtime_error(
            "Could not read a complete update archive entry name");
      const std::string member(filename.data(), filename.size() - 1);
      if (member.find('\0') != std::string::npos)
        throw std::runtime_error(
            "Update archive entry contains an embedded null");

      const bool is_directory =
          mz_zip_reader_is_file_a_directory(&zip, i) != 0;
      if (!IsSafeUpdaterRelativePath(member, is_directory))
        throw std::runtime_error("Unsafe update archive path: " + member);
      const std::string canonical_member =
          CanonicalUpdaterRelativePath(member);
      const std::string collision_key =
          UpdaterPathCollisionKey(canonical_member);
      const auto [previous, inserted] =
          seen_member_paths.emplace(collision_key, canonical_member);
      if (!inserted)
        throw std::runtime_error("Colliding update archive paths: " +
                                 previous->second + " and " + canonical_member);

      ValidateUpdaterZipEntryAttributes(
          canonical_member, is_directory, stat.m_version_made_by,
          stat.m_external_attr);
      if (is_directory) {
        if (stat.m_uncomp_size != 0 ||
            expected_directories.find(collision_key) ==
                expected_directories.end())
          throw std::runtime_error(
              "Update archive contains an unexpected directory: " +
              canonical_member);
      } else {
        uint64_t maximum_file_size = kMaxUpdateFileBytes;
        const auto expected = expected_files.find(collision_key);
        if (expected != expected_files.end()) {
          if (stat.m_uncomp_size != expected->second.size)
            throw std::runtime_error(
                "Update archive member size does not match its signed manifest: " +
                canonical_member);
          if (UpdaterZipEntryMode(false, stat.m_version_made_by,
                                  stat.m_external_attr) !=
              expected->second.mode)
            throw std::runtime_error(
                "Update archive member mode does not match its signed manifest: " +
                canonical_member);
          observed_managed_files.insert(collision_key);
        } else if (collision_key == local_manifest_key) {
          maximum_file_size = kMaxLocalManifestBytes;
          observed_local_manifest = true;
        } else {
          throw std::runtime_error(
              "Update archive contains an unmanaged file: " + canonical_member);
        }
        const uint64_t member_size = stat.m_uncomp_size;
        if (member_size > maximum_file_size ||
            expanded_size > kMaxExpandedUpdateBytes - member_size)
          throw std::runtime_error(
              "Update archive exceeds expanded payload limits");
        expanded_size += member_size;
      }
      entries.push_back({i, canonical_member, is_directory,
                         stat.m_version_made_by, stat.m_external_attr});
    }
    if (observed_managed_files.size() != expected_files.size() ||
        !observed_local_manifest)
      throw std::runtime_error(
          "Update archive omits files required by its signed manifest");

    for (size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
      const ValidatedZipEntry &entry = entries[entry_index];
      const fs::path target = stage_dir / Utf8Path(entry.path);
      if (entry.directory) {
        fs::create_directories(target);
      } else {
        fs::create_directories(target.parent_path());
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
          throw std::runtime_error("Failed opening extracted file " +
                                   entry.path);
        ZipWriteContext write_context{&output};
        const bool extracted =
            mz_zip_reader_extract_to_callback(&zip, entry.index,
                                              WriteZipBytes, &write_context, 0);
        output.flush();
        const bool write_failed = !output.good();
        output.close();
        if (!extracted || write_failed) {
          std::error_code ignored;
          fs::remove(target, ignored);
          throw std::runtime_error("Failed extracting " + entry.path);
        }
      }
      ApplyUpdaterZipEntryAttributes(
          target, entry.directory, entry.version_made_by,
          entry.external_attributes);
      if (ui) {
        ui->SetProgress("Extracting update payload",
                        static_cast<uint64_t>(entry_index + 1),
                        static_cast<uint64_t>(entries.size()));
        if (!ui->Pump())
          throw std::runtime_error("Extraction cancelled");
      }
    }
  } catch (...) {
    mz_zip_reader_end(&zip);
    throw;
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

std::map<std::string, FileEntry>
BuildManifestFileMap(const std::vector<FileEntry> &files) {
  std::map<std::string, FileEntry> out;
  for (const FileEntry &file : files) {
    FileEntry normalized_file = file;
    normalized_file.path =
        RequireSafeUpdaterPath(file.path, "manifest file path");
    const std::string key = UpdaterPathCollisionKey(normalized_file.path);
    if (!out.emplace(key, std::move(normalized_file)).second)
      throw std::runtime_error("Duplicate manifest file path: " + file.path);
  }
  return out;
}

void ValidateRemotePayloadPaths(const RemotePayload &payload) {
  RequireSafeUpdaterPath(payload.launch_exe, "release launch path");
  RequireSafeUpdaterPath(payload.engine_library, "release engine path");
  RequireSafeUpdaterFileName(payload.update_manifest_name,
                             "release manifest asset name");
  RequireSafeUpdaterFileName(payload.update_package_name,
                             "release package asset name");
  RequireSafeUpdaterFileName(payload.local_manifest_name,
                             "release local manifest name");
  if (payload.local_manifest_name != kLocalManifestName)
    throw std::runtime_error(
        "Release local manifest name does not match the bootstrap contract");
}

bool LiveFileMatchesEntry(const AnchoredFileTree &installed_files,
                          const FileEntry &entry) {
  return installed_files.FileMatches(
      entry.path,
      MakeInstallFileExpectation(entry.size, entry.sha256, entry.mode));
}

void ValidateRuntimeCoordinationLock(
    const std::map<std::string, FileEntry> &local_files,
    const std::map<std::string, FileEntry> &remote_files,
    const AnchoredFileTree &installed_files) {
  const std::string key =
      UpdaterPathCollisionKey(kRuntimeCoordinationLockName);
  const auto local = local_files.find(key);
  const auto remote = remote_files.find(key);
  const auto valid_entry = [](const FileEntry &entry) {
    return entry.path == kRuntimeCoordinationLockName && entry.size == 0 &&
           ToLower(entry.sha256) == kEmptySha256 && entry.mode == 0644;
  };
  if (local == local_files.end() || remote == remote_files.end() ||
      !valid_entry(local->second) || !valid_entry(remote->second) ||
      !LiveFileMatchesEntry(installed_files, local->second))
    throw std::runtime_error(
        "The stable runtime/update coordination lock is missing or changed; "
        "repair this installation before updating");
}

InstallSyncPlan BuildInstallSyncPlan(const fs::path &install_root,
                                     const InstallManifest &local_manifest,
                                     const UpdaterConfig &config,
                                     const RemotePayload &payload) {
  ValidateRemotePayloadPaths(payload);
  ValidateRemotePayloadManifest(payload);
  InstallSyncPlan plan;
  plan.version_change =
      CompareSemver(local_manifest.version, payload.version) < 0;
  plan.metadata_change =
      local_manifest.version != payload.version ||
      local_manifest.launch_exe != payload.launch_exe ||
      local_manifest.engine_library != payload.engine_library ||
      local_manifest.local_manifest_name != payload.local_manifest_name ||
      !local_manifest.authenticated_files ||
      local_manifest.authenticated_release_sequence !=
          payload.release_sequence ||
      local_manifest.authenticated_release_index_sha256 !=
          payload.release_index_sha256;

  const std::vector<FileEntry> remote_files =
      ManifestFiles(payload.manifest_json);
  const auto remote_map = BuildManifestFileMap(remote_files);
  const auto local_map = BuildManifestFileMap(local_manifest.files);
  AnchoredFileTree installed_files(fs::canonical(install_root));
  ValidateRuntimeCoordinationLock(local_map, remote_map, installed_files);

  for (const FileEntry &remote_file : remote_files) {
    const std::string normalized = UpdaterPathCollisionKey(remote_file.path);
    const auto local_it = local_map.find(normalized);
    const bool manifest_matches =
        local_it != local_map.end() &&
        local_it->second.size == remote_file.size &&
        ToLower(local_it->second.sha256) == ToLower(remote_file.sha256) &&
        local_it->second.mode == remote_file.mode;
    if (manifest_matches &&
        LiveFileMatchesEntry(installed_files, remote_file)) {
      ++plan.unchanged_count;
      continue;
    }

    InstallAction action;
    action.kind = local_it == local_map.end() ? InstallActionKind::Add
                                              : InstallActionKind::Refresh;
    action.file = remote_file;
    plan.install_actions.push_back(std::move(action));
  }

  if (!local_manifest.authenticated_files && !local_map.empty()) {
    BootstrapTrace(
        "Stale-file removal disabled because the prior install manifest is "
        "not authenticated");
  } else {
    for (const auto &[normalized, local_file] : local_map) {
      if (remote_map.count(normalized) != 0)
        continue;
      if (IsPreserved(local_file.path, config))
        continue;
      try {
        if (LiveFileMatchesEntry(installed_files, local_file))
          plan.remove_paths.push_back(local_file.path);
      } catch (const std::exception &error) {
        BootstrapTrace("Preserving unauthenticated stale-file candidate " +
                       local_file.path + ": " + error.what());
      }
    }
  }

  return plan;
}

std::string DescribeInstallSyncPlan(const InstallSyncPlan &plan) {
  std::vector<std::string> parts;
  if (plan.AddCount() != 0)
    parts.push_back(std::to_string(plan.AddCount()) + " add");
  if (plan.RefreshCount() != 0)
    parts.push_back(std::to_string(plan.RefreshCount()) + " refresh");
  if (!plan.remove_paths.empty())
    parts.push_back(std::to_string(plan.remove_paths.size()) + " remove");
  if (plan.metadata_change && parts.empty())
    parts.push_back("metadata refresh");
  if (parts.empty())
    return "no file changes";

  std::ostringstream ss;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0)
      ss << ", ";
    ss << parts[i];
  }
  return ss.str();
}

bool PlanTouchesPath(const InstallSyncPlan &plan, const std::string &rel_path) {
  const std::string normalized = UpdaterPathCollisionKey(rel_path);
  for (const InstallAction &action : plan.install_actions) {
    if (UpdaterPathCollisionKey(action.file.path) == normalized)
      return true;
  }
  for (const std::string &remove_path : plan.remove_paths) {
    if (UpdaterPathCollisionKey(remove_path) == normalized)
      return true;
  }
  return false;
}

bool CanApplySyncInProcess(const BootstrapOptions &options,
                           const InstallSyncPlan &plan) {
  if (options.worker_mode)
    return true;
  if (options.role != Role::Client)
    return false;
  if (PlanTouchesPath(plan, options.launch_relpath))
    return false;
  return true;
}

Json::Value BuildLocalInstallManifest(const RemotePayload &payload) {
  Json::Value root = payload.manifest_json;
  root.removeMember("package");
  root["install_state"] = true;
  root["launch_exe"] = payload.launch_exe;
  root["engine_library"] = payload.engine_library;
  root["local_manifest_name"] = payload.local_manifest_name;
  Json::Value authentication(Json::objectValue);
  authentication["schema_version"] = 1;
  authentication["release_index_name"] = payload.release_index_name;
  authentication["raw_release_index"] = payload.raw_release_index;
  authentication["raw_release_signature"] = payload.raw_release_signature;
  authentication["raw_update_manifest"] = payload.raw_update_manifest;
  root["installed_release_authentication"] = std::move(authentication);
  return root;
}

void ValidateStagedPayload(const fs::path &stage_dir,
                           const InstallSyncPlan &plan) {
  AnchoredFileTree staged_files(fs::canonical(stage_dir));
  for (const InstallAction &action : plan.install_actions) {
    const FileEntry &file = action.file;
    if (!staged_files.FileMatches(
            file.path,
            MakeInstallFileExpectation(file.size, file.sha256, file.mode)))
      throw std::runtime_error("Missing or mismatched staged file " + file.path);
  }
}

void ApplyInstallSyncPlan(const fs::path &stage_dir,
                          const fs::path &install_root,
                          const InstallSyncPlan &plan,
                          const RemotePayload &payload,
                          const std::string &predecessor_manifest,
                          BootstrapUi *ui) {
  ValidateRemotePayloadPaths(payload);
  if (!plan.RequiresSync())
    return;

  InstallTransactionPlan transaction;
  transaction.manifest_path = payload.local_manifest_name;
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  ";
  transaction.manifest_contents =
      Json::writeString(writer, BuildLocalInstallManifest(payload));
  transaction.predecessor_manifest_size = predecessor_manifest.size();
  transaction.predecessor_manifest_sha256 = Sha256Hex(
      reinterpret_cast<const uint8_t *>(predecessor_manifest.data()),
      predecessor_manifest.size());
  if (transaction.manifest_contents.size() > kMaxLocalManifestBytes)
    throw std::runtime_error(
        "Authenticated local install manifest exceeds its supported size");

  transaction.install_files.reserve(plan.install_actions.size());
  for (const InstallAction &action : plan.install_actions) {
    transaction.install_files.push_back(
        {RequireSafeUpdaterPath(action.file.path, "install action path"),
         action.file.size, action.file.sha256, action.file.mode});
  }
  transaction.remove_paths.reserve(plan.remove_paths.size());
  for (const std::string &remove_path : plan.remove_paths) {
    transaction.remove_paths.push_back(
        RequireSafeUpdaterPath(remove_path, "install removal path"));
  }

  InstallTransactionOptions options;
  options.progress = [ui](size_t completed, size_t total) {
    if (!ui)
      return;
    if (!ui->SetProgress("Synchronizing installation",
                         static_cast<uint64_t>(completed),
                         static_cast<uint64_t>(std::max<size_t>(1, total))) ||
        !ui->Pump())
      throw std::runtime_error("Install cancelled");
  };
  ApplyInstallTransaction(stage_dir, install_root, transaction, options);
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

SDL_Process *
SpawnProcess(const fs::path &working_dir, std::vector<std::string> args,
             const std::vector<std::pair<std::string, std::string>> &env,
             bool background) {
  std::vector<char *> arg_ptrs = BuildArgPointers(args);
  SDL_PropertiesID props = SDL_CreateProperties();
  if (!props)
    throw std::runtime_error(SDL_GetError());

  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                         arg_ptrs.data());
  SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
                        GenericPath(working_dir).c_str());
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN,
                         background);
  if (background) {
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                          SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                          SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                          SDL_PROCESS_STDIO_NULL);
  }

  SDL_Environment *environment = nullptr;
  if (!env.empty()) {
    environment = SDL_CreateEnvironment(true);
    if (!environment) {
      SDL_DestroyProperties(props);
      throw std::runtime_error(SDL_GetError());
    }
    for (const auto &pair : env) {
      SDL_SetEnvironmentVariable(environment, pair.first.c_str(),
                                 pair.second.c_str(), true);
    }
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,
                           environment);
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
  if (AttachConsole(ATTACH_PARENT_PROCESS) ||
      GetLastError() == ERROR_ACCESS_DENIED) {
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
  const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                      sizeof(elevation), &size);
  CloseHandle(token);
  return ok && elevation.TokenIsElevated != 0;
}

bool IsPermissionDeniedError(const std::error_code &error) {
  if (error == std::errc::permission_denied)
    return true;
  if (error.default_error_condition() == std::errc::permission_denied)
    return true;
  return error.value() == ERROR_ACCESS_DENIED ||
         error.value() == ERROR_SHARING_VIOLATION ||
         error.value() == ERROR_LOCK_VIOLATION;
}

bool CurrentTokenHasPathAccess(const fs::path &path, DWORD access,
                               bool directory) {
  const DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT |
                      (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
  const HANDLE handle =
      CreateFileW(path.c_str(), access,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
    return true;
  }

  const DWORD error = GetLastError();
  if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)
    return false;
  throw fs::filesystem_error("Could not validate updater elevation access",
                             path,
                             std::error_code(error, std::system_category()));
}

fs::path RequireProtectedElevationRoot(
    const fs::path &install_root, const std::string &approved_config_sha256,
    const InstallManifest *installed_manifest = nullptr) {
  const fs::path canonical_root = fs::canonical(install_root);
  AnchoredFileTree installed_files(canonical_root);
  const std::string raw_config =
      installed_files.ReadFile(kConfigName, kMaxUpdaterConfigBytes);
  const std::string current_digest = Sha256Hex(
      reinterpret_cast<const uint8_t *>(raw_config.data()), raw_config.size());
  if (!IsSha256Hex(approved_config_sha256) ||
      current_digest != approved_config_sha256)
    throw std::runtime_error("Updater configuration changed before elevation");

  const fs::path config_path = canonical_root / kConfigName;
  for (const DWORD access : std::array<DWORD, 5>{
           FILE_WRITE_DATA, FILE_APPEND_DATA, DELETE, WRITE_DAC, WRITE_OWNER}) {
    if (CurrentTokenHasPathAccess(config_path, access, false))
      throw std::runtime_error(
          "Refusing elevation because the updater trust configuration is "
          "writable by the unelevated process");
  }

  const fs::path manifest_path = canonical_root / kLocalManifestName;
  for (const DWORD access : std::array<DWORD, 5>{
           FILE_WRITE_DATA, FILE_APPEND_DATA, DELETE, WRITE_DAC, WRITE_OWNER}) {
    if (CurrentTokenHasPathAccess(manifest_path, access, false))
      throw std::runtime_error(
          "Refusing elevation because authenticated installed-release "
          "evidence is writable by the unelevated process");
  }

  if (!installed_files.HasRegularFile(kUpdaterStem))
    throw std::runtime_error(
        "Protected install is missing its updater worker executable");
  const fs::path worker_path = canonical_root / kUpdaterStem;
  for (const DWORD access : std::array<DWORD, 5>{
           FILE_WRITE_DATA, FILE_APPEND_DATA, DELETE, WRITE_DAC, WRITE_OWNER}) {
    if (CurrentTokenHasPathAccess(worker_path, access, false))
      throw std::runtime_error(
          "Refusing elevation because the updater worker is writable by the "
          "unelevated process");
  }
  if (installed_manifest && installed_manifest->authenticated_files) {
    const std::string worker_key = UpdaterPathCollisionKey(kUpdaterStem);
    const auto worker_entry = std::find_if(
        installed_manifest->files.begin(), installed_manifest->files.end(),
        [&worker_key](const FileEntry &file) {
          return UpdaterPathCollisionKey(file.path) == worker_key;
        });
    if (worker_entry == installed_manifest->files.end() ||
        !installed_files.FileMatches(
            worker_entry->path, MakeInstallFileExpectation(
                                    worker_entry->size, worker_entry->sha256,
                                    worker_entry->mode)))
      throw std::runtime_error(
          "Protected updater worker does not match authenticated installed "
          "release evidence");
  }

  for (const DWORD access : std::array<DWORD, 3>{
           FILE_ADD_FILE, FILE_ADD_SUBDIRECTORY, FILE_DELETE_CHILD}) {
    if (CurrentTokenHasPathAccess(canonical_root, access, true))
      throw std::runtime_error(
          "Refusing elevation because the install root is mutable by the "
          "unelevated process");
  }

  fs::path child = canonical_root;
  while (true) {
    for (const DWORD access :
         std::array<DWORD, 3>{DELETE, WRITE_DAC, WRITE_OWNER}) {
      if (CurrentTokenHasPathAccess(child, access, true))
        throw std::runtime_error(
            "Refusing elevation because an install-root ancestor is mutable "
            "by the unelevated process");
    }
    const fs::path parent = child.parent_path();
    if (parent.empty() || parent == child)
      break;
    if (CurrentTokenHasPathAccess(parent, FILE_DELETE_CHILD, true))
      throw std::runtime_error(
          "Refusing elevation because an install-root ancestor can be "
          "replaced by the unelevated process");
    child = parent;
  }
  return canonical_root;
}

std::wstring Utf8ToWide(const std::string &text) {
  if (text.empty())
    return {};
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
  if (!result.empty())
    result.pop_back();
  return result;
}

std::string WideToUtf8(const wchar_t *text) {
  if (!text || !*text)
    return {};
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr,
                      nullptr);
  if (!result.empty())
    result.pop_back();
  return result;
}

std::wstring QuoteWindowsArg(const std::string &arg) {
  const std::wstring wide = Utf8ToWide(arg);
  if (!wide.empty() && wide.find_first_of(L" \t\"") == std::wstring::npos)
    return wide;
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (const wchar_t ch : wide) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }
    if (ch == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
    } else {
      out.append(backslashes, L'\\');
      out.push_back(ch);
    }
    backslashes = 0;
  }
  // Backslashes immediately before the closing quote must be doubled or the
  // Windows argv parser treats the quote itself as escaped.
  out.append(backslashes * 2, L'\\');
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
  if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(),
                       &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
    return INVALID_HANDLE_VALUE;
  return duplicate;
}

void CloseIfValid(HANDLE handle) {
  if (handle && handle != INVALID_HANDLE_VALUE)
    CloseHandle(handle);
}

void LaunchWindowsProcess(const fs::path &working_dir,
                          const std::vector<std::string> &args, bool background,
                          bool new_process_group) {
  const std::wstring cwd = working_dir.wstring();
  std::wstring command_line = BuildWindowsCommandLine(args);
  STARTUPINFOEXW startup_info{};
  startup_info.StartupInfo.cb = sizeof(startup_info);
  startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;

  HANDLE stdin_handle = INVALID_HANDLE_VALUE;
  HANDLE stdout_handle = INVALID_HANDLE_VALUE;
  HANDLE stderr_handle = INVALID_HANDLE_VALUE;
  SECURITY_ATTRIBUTES security_attributes{};
  security_attributes.nLength = sizeof(security_attributes);
  security_attributes.bInheritHandle = TRUE;

  if (background) {
    stdin_handle = CreateFileW(L"\\\\.\\NUL", GENERIC_READ | GENERIC_WRITE, 0,
                               &security_attributes, OPEN_EXISTING, 0, nullptr);
    stdout_handle =
        CreateFileW(L"\\\\.\\NUL", GENERIC_READ | GENERIC_WRITE, 0,
                    &security_attributes, OPEN_EXISTING, 0, nullptr);
    stderr_handle =
        CreateFileW(L"\\\\.\\NUL", GENERIC_READ | GENERIC_WRITE, 0,
                    &security_attributes, OPEN_EXISTING, 0, nullptr);
  } else {
    stdin_handle = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security_attributes, OPEN_EXISTING, 0, nullptr);
    stdout_handle =
        CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security_attributes,
                    OPEN_EXISTING, 0, nullptr);
    stderr_handle =
        CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security_attributes,
                    OPEN_EXISTING, 0, nullptr);
    if (stdin_handle == INVALID_HANDLE_VALUE ||
        stdout_handle == INVALID_HANDLE_VALUE ||
        stderr_handle == INVALID_HANDLE_VALUE) {
      CloseIfValid(stdin_handle);
      CloseIfValid(stdout_handle);
      CloseIfValid(stderr_handle);
      stdin_handle = DuplicateInheritedHandle(GetStdHandle(STD_INPUT_HANDLE));
      stdout_handle = DuplicateInheritedHandle(GetStdHandle(STD_OUTPUT_HANDLE));
      stderr_handle = DuplicateInheritedHandle(GetStdHandle(STD_ERROR_HANDLE));
    }
  }

  if (stdin_handle == INVALID_HANDLE_VALUE ||
      stdout_handle == INVALID_HANDLE_VALUE ||
      stderr_handle == INVALID_HANDLE_VALUE) {
    CloseIfValid(stdin_handle);
    CloseIfValid(stdout_handle);
    CloseIfValid(stderr_handle);
    throw std::runtime_error(
        "Could not create inheritable process I/O handles");
  }

  startup_info.StartupInfo.hStdInput = stdin_handle;
  startup_info.StartupInfo.hStdOutput = stdout_handle;
  startup_info.StartupInfo.hStdError = stderr_handle;

  std::array<HANDLE, 3> inherited_handles = {
      stdin_handle,
      stdout_handle,
      stderr_handle,
  };
  SIZE_T attribute_bytes = 0;
  (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
  if (attribute_bytes == 0) {
    CloseIfValid(stdin_handle);
    CloseIfValid(stdout_handle);
    CloseIfValid(stderr_handle);
    throw std::runtime_error(
        "Could not size the process handle allowlist");
  }
  std::vector<unsigned char> attribute_storage;
  try {
    attribute_storage.resize(attribute_bytes);
  } catch (...) {
    CloseIfValid(stdin_handle);
    CloseIfValid(stdout_handle);
    CloseIfValid(stderr_handle);
    throw;
  }
  startup_info.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
          attribute_storage.data());
  if (!InitializeProcThreadAttributeList(startup_info.lpAttributeList, 1, 0,
                                         &attribute_bytes)) {
    CloseIfValid(stdin_handle);
    CloseIfValid(stdout_handle);
    CloseIfValid(stderr_handle);
    throw std::runtime_error(
        "Could not restrict inherited process handles");
  }
  if (!UpdateProcThreadAttribute(
          startup_info.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          inherited_handles.data(),
          inherited_handles.size() * sizeof(HANDLE), nullptr, nullptr)) {
    DeleteProcThreadAttributeList(startup_info.lpAttributeList);
    CloseIfValid(stdin_handle);
    CloseIfValid(stdout_handle);
    CloseIfValid(stderr_handle);
    throw std::runtime_error(
        "Could not populate the inherited process handle allowlist");
  }

  PROCESS_INFORMATION process_info{};
  DWORD creation_flags =
      CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
  if (background)
    creation_flags |= CREATE_NO_WINDOW;
  if (new_process_group)
    creation_flags |= CREATE_NEW_PROCESS_GROUP;

  const BOOL ok = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr,
                                 TRUE, creation_flags, nullptr, cwd.c_str(),
                                 &startup_info.StartupInfo, &process_info);
  const DWORD create_error = ok ? ERROR_SUCCESS : GetLastError();

  DeleteProcThreadAttributeList(startup_info.lpAttributeList);

  CloseIfValid(stdin_handle);
  CloseIfValid(stdout_handle);
  CloseIfValid(stderr_handle);

  if (!ok)
    throw std::runtime_error("CreateProcess failed: " +
                             std::to_string(create_error));

  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
}

bool RelaunchElevated(const fs::path &exe_path,
                      const std::vector<std::string> &args,
                      const fs::path &working_dir) {
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

uint64_t FileTimeValue(const FILETIME &value) {
  ULARGE_INTEGER combined{};
  combined.LowPart = value.dwLowDateTime;
  combined.HighPart = value.dwHighDateTime;
  return combined.QuadPart;
}

uint64_t ProcessCreationTime(HANDLE process) {
  FILETIME creation{};
  FILETIME exit_time{};
  FILETIME kernel{};
  FILETIME user{};
  if (!GetProcessTimes(process, &creation, &exit_time, &kernel, &user))
    throw std::runtime_error("Could not read updater handoff process identity");
  return FileTimeValue(creation);
}

uint64_t CurrentProcessCreationTime() {
  return ProcessCreationTime(GetCurrentProcess());
}

void WaitForBootstrapProcessExit(uint32_t process_id,
                                 uint64_t expected_creation_time) {
  if (process_id == 0 || expected_creation_time == 0 ||
      process_id == GetCurrentProcessId())
    throw std::runtime_error("Invalid updater handoff process identity");
  const HANDLE process = OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
      static_cast<DWORD>(process_id));
  if (!process) {
    if (GetLastError() == ERROR_INVALID_PARAMETER)
      return;
    throw std::runtime_error("Could not open updater handoff process");
  }

  uint64_t observed_creation_time = 0;
  try {
    observed_creation_time = ProcessCreationTime(process);
  } catch (...) {
    CloseHandle(process);
    throw;
  }
  if (observed_creation_time != expected_creation_time) {
    CloseHandle(process);
    BootstrapTrace(
        "Updater handoff predecessor exited before identity validation");
    return;
  }

  const DWORD result = WaitForSingleObject(process, 60 * 1000);
  const DWORD wait_error =
      result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
  CloseHandle(process);
  if (result == WAIT_TIMEOUT)
    throw std::runtime_error("Timed out waiting for updater handoff process");
  if (result != WAIT_OBJECT_0)
    throw std::runtime_error("Could not wait for updater handoff process: " +
                             std::to_string(wait_error));
}

fs::path CurrentProcessImagePath() {
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size())
    throw std::runtime_error("Could not resolve the updater process image");
  return fs::canonical(fs::path(buffer.data(), buffer.data() + length));
}

void ValidatePrivilegedWorkerIdentity(const BootstrapOptions &options,
                                      std::string_view expected_worker_path,
                                      bool require_authenticated_evidence) {
  const fs::path canonical_root = fs::canonical(options.install_root);
  const fs::path process_image = CurrentProcessImagePath();
  const fs::path expected_image =
      canonical_root / Utf8Path(std::string(expected_worker_path));
  if (!fs::equivalent(process_image, expected_image))
    throw std::runtime_error(
        "Privileged updater image is not the protected worker requested by "
        "the install root");
  ValidateProtectedElevationBoundary(canonical_root, expected_worker_path);

  if (!require_authenticated_evidence)
    return;
  ValidateProtectedElevationBoundary(canonical_root, kConfigName);
  ValidateProtectedElevationBoundary(canonical_root, kLocalManifestName);
  AnchoredFileTree installed_files(canonical_root);
  const std::string raw_config =
      installed_files.ReadFile(kConfigName, kMaxUpdaterConfigBytes);
  const std::string config_sha256 = Sha256Hex(
      reinterpret_cast<const uint8_t *>(raw_config.data()), raw_config.size());
  if (!IsSha256Hex(options.approved_config_sha256) ||
      config_sha256 != options.approved_config_sha256)
    throw std::runtime_error(
        "Privileged updater configuration does not match user approval");
  const UpdaterConfig config = LoadUpdaterConfig(raw_config, options.role);
  if (config.allow_unsigned_development_index)
    throw std::runtime_error(
        "Privileged updater workers cannot use unsigned development metadata");
  const InstallManifest manifest = LoadInstallManifest(
      installed_files.ReadFile(kLocalManifestName, kMaxLocalManifestBytes),
      config, options);
  if (!manifest.authenticated_files)
    throw std::runtime_error(
        "Privileged updater has no authenticated installed-release evidence");
  const std::string updater_key = UpdaterPathCollisionKey(kUpdaterStem);
  const auto updater_entry = std::find_if(
      manifest.files.begin(), manifest.files.end(),
      [&updater_key](const FileEntry &entry) {
        return UpdaterPathCollisionKey(entry.path) == updater_key;
      });
  if (updater_entry == manifest.files.end() ||
      !installed_files.FileMatches(
          expected_worker_path,
          MakeInstallFileExpectation(updater_entry->size,
                                     updater_entry->sha256,
                                     updater_entry->mode)))
    throw std::runtime_error(
        "Privileged updater image does not match authenticated release evidence");
}
#endif

void BootstrapReadyDismiss(void *userdata) {
  auto *ui = static_cast<UiHandle *>(userdata);
  if (ui)
    ui->DismissForEngineHandoff();
}

void SetProcessEnvVar(const char *name, const std::string &value) {
#if defined(_WIN32)
  if (!SetEnvironmentVariableW(Utf8ToWide(name).c_str(),
                               Utf8ToWide(value).c_str()))
    throw std::runtime_error("Failed setting environment variable " +
                             std::string(name));
#else
  if (setenv(name, value.c_str(), 1) != 0)
    throw std::runtime_error("Failed setting environment variable " +
                             std::string(name));
#endif
}

void ClearProcessEnvVar(const char *name) {
#if defined(_WIN32)
  SetEnvironmentVariableW(Utf8ToWide(name).c_str(), nullptr);
#else
  unsetenv(name);
#endif
}

int LaunchEngineAndWait(const fs::path &install_root,
                        const std::string &launch_relpath,
                        const std::string &engine_library_relpath,
                        const std::vector<std::string> &forwarded_args,
                        UiHandle *ui) {
  AnchoredFileTree runtime_files(fs::canonical(install_root));
  AnchoredFileLock runtime_lock =
      runtime_files.AcquireExistingFileSharedLock(
          kRuntimeCoordinationLockName);
  const InstallFileExpectation runtime_lock_expectation =
      MakeInstallFileExpectation(0, kEmptySha256, 0644);
  if (!runtime_files.FileMatches(kRuntimeCoordinationLockName,
                                 runtime_lock_expectation))
    throw std::runtime_error(
        "The stable runtime/update coordination lock is invalid");

  const fs::path engine_path = install_root / Utf8Path(engine_library_relpath);
  if (!fs::is_regular_file(engine_path))
    throw std::runtime_error("Engine library not found: " +
                             GenericPath(engine_path));

  if (ui && ui->Get())
    ui->Get()->SetStatus("Launching WORR",
                         "Starting the hosted engine library.");
  if (ui && ui->Get() && !ui->Get()->WaitForMinimumDisplayTime())
    return 1;

  auto *engine_object = SDL_LoadObject(GenericPath(engine_path).c_str());
  if (!engine_object)
    throw std::runtime_error("Could not load engine library " +
                             GenericPath(engine_path) + ": " + SDL_GetError());

  auto *engine_main = reinterpret_cast<engine_main_fn>(
      SDL_LoadFunction(engine_object, kEngineEntryPoint));
  if (!engine_main) {
    const std::string error = SDL_GetError();
    SDL_UnloadObject(engine_object);
    throw std::runtime_error("Engine library is missing " +
                             std::string(kEngineEntryPoint) + ": " + error);
  }

  auto *set_ready_callback = reinterpret_cast<set_ready_callback_fn>(
      SDL_LoadFunction(engine_object, kReadyCallbackEntryPoint));

  bool shared_window_handoff = false;
  auto *splash_ui =
      (ui && ui->Get()) ? dynamic_cast<SplashUi *>(ui->Get()) : nullptr;
  if (splash_ui && splash_ui->SupportsSharedWindowHandoff()) {
    if (void *native_handle = splash_ui->GetSharedWindowNativeHandle()) {
      if (splash_ui->PrepareSharedWindowHandoff()) {
        SetProcessEnvVar(kBootstrapWin32HwndEnv, PointerString(native_handle));
        shared_window_handoff = true;
        BootstrapTrace("LaunchEngineAndWait shared_window_handoff=1 hwnd=" +
                       PointerString(native_handle));
      }
    }
  }
  if (!shared_window_handoff)
    BootstrapTrace("LaunchEngineAndWait shared_window_handoff=0");

  if (!shared_window_handoff) {
    if (!set_ready_callback && ui)
      ui->DismissForEngineHandoff();
    else if (set_ready_callback && ui && ui->Get())
      set_ready_callback(BootstrapReadyDismiss, ui);
  } else if (set_ready_callback) {
    set_ready_callback(nullptr, nullptr);
  }

  std::vector<std::string> args;
  args.push_back(GenericPath(install_root / Utf8Path(launch_relpath)));
  args.insert(args.end(), forwarded_args.begin(), forwarded_args.end());
  std::vector<char *> arg_ptrs = BuildArgPointers(args);

  SetProcessEnvVar(kBaseDirEnv, GenericPath(install_root));
  const bool bootstrap_transition =
      ui && dynamic_cast<SplashUi *>(ui->Get()) != nullptr;
  if (bootstrap_transition)
    SetProcessEnvVar(kBootstrapTransitionEnv, "1");

  int exit_code = 0;
  try {
    exit_code = engine_main(static_cast<int>(args.size()), arg_ptrs.data());
  } catch (...) {
    if (set_ready_callback)
      set_ready_callback(nullptr, nullptr);
    SDL_UnloadObject(engine_object);
    ClearProcessEnvVar(kBaseDirEnv);
    if (bootstrap_transition)
      ClearProcessEnvVar(kBootstrapTransitionEnv);
    if (shared_window_handoff)
      ClearProcessEnvVar(kBootstrapWin32HwndEnv);
    throw;
  }

  if (set_ready_callback)
    set_ready_callback(nullptr, nullptr);
  SDL_UnloadObject(engine_object);
  ClearProcessEnvVar(kBaseDirEnv);
  if (bootstrap_transition)
    ClearProcessEnvVar(kBootstrapTransitionEnv);
  if (shared_window_handoff)
    ClearProcessEnvVar(kBootstrapWin32HwndEnv);
  return exit_code;
}

std::vector<std::string> BuildWorkerInvocation(const BootstrapOptions &options,
                                               bool approved_install) {
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
  if (approved_install) {
    if (!IsSha256Hex(options.approved_config_sha256) ||
        !IsSha256Hex(options.approved_release_index_sha256) ||
        options.approved_release_sequence == 0)
      throw std::runtime_error(
          "Approved updater worker is missing its immutable approval binding");
    args.push_back("--approved-install");
    args.push_back("--approved-config-sha256");
    args.push_back(options.approved_config_sha256);
    args.push_back("--approved-release-index-sha256");
    args.push_back(options.approved_release_index_sha256);
    args.push_back("--approved-release-sequence");
    args.push_back(std::to_string(options.approved_release_sequence));
  }
  if (options.stage_elevated_worker)
    args.push_back("--stage-elevated-worker");
  if ((options.wait_for_process_id == 0) !=
      (options.wait_for_process_creation_time == 0))
    throw std::runtime_error("Incomplete updater handoff process identity");
  if (options.wait_for_process_id != 0) {
    args.push_back("--wait-for-process");
    args.push_back(std::to_string(options.wait_for_process_id));
    args.push_back("--wait-for-process-creation-time");
    args.push_back(
        std::to_string(options.wait_for_process_creation_time));
  }
  if (options.recover_only)
    args.push_back("--recover-only");
  if (options.quiet_status)
    args.push_back(kQuietStatusArg);
  if (!options.forwarded_args.empty()) {
    args.push_back("--");
    args.insert(args.end(), options.forwarded_args.begin(),
                options.forwarded_args.end());
  }
  BootstrapTrace("BuildWorkerInvocation approved_install=" +
                 std::to_string(approved_install ? 1 : 0) + " quiet_status=" +
                 std::to_string(options.quiet_status ? 1 : 0) +
                 " argc=" + std::to_string(args.size()));
  return args;
}

int LaunchApprovedUpdateWorker(const BootstrapOptions &options,
                               const std::string &config_sha256,
                               const RemotePayload &approved_payload,
                               const InstallManifest &local_manifest) {
  BootstrapTrace("LaunchApprovedUpdateWorker role=" +
                 std::string(RoleToCString(options.role)) +
                 " install_root=" + GenericPath(options.install_root));
  if (!local_manifest.authenticated_files)
    throw std::runtime_error(
        "Refusing to hand off to an updater worker without authenticated "
        "installed-release evidence");
  const std::string worker_key = UpdaterPathCollisionKey(kUpdaterStem);
  const auto worker_entry = std::find_if(
      local_manifest.files.begin(), local_manifest.files.end(),
      [&worker_key](const FileEntry &file) {
        return UpdaterPathCollisionKey(file.path) == worker_key;
      });
  if (worker_entry == local_manifest.files.end())
    throw std::runtime_error(
        "Authenticated installed-release evidence omits the updater worker");

  AnchoredFileTree installed_files(fs::canonical(options.install_root));
  const InstallFileExpectation worker_expectation =
      MakeInstallFileExpectation(worker_entry->size, worker_entry->sha256,
                                 worker_entry->mode);
  if (!installed_files.FileMatches(worker_entry->path, worker_expectation))
    throw std::runtime_error(
        "Installed updater worker does not match authenticated release evidence");

  const fs::path temp_worker =
      fs::temp_directory_path() /
      ("worr-updater-" + RandomToken() + fs::path(kUpdaterStem).extension().string());
  AnchoredFileTree temporary_files(fs::canonical(temp_worker.parent_path()));
  if (!temporary_files.ReplaceFileFrom(
          installed_files, worker_entry->path,
          temp_worker.filename().generic_u8string(), &worker_expectation))
    throw std::runtime_error(
        "Authenticated updater worker disappeared during secure handoff");
  BootstrapTrace("LaunchApprovedUpdateWorker copied temp_worker=" +
                 GenericPath(temp_worker));

  std::vector<std::string> args;
  args.push_back(GenericPath(temp_worker));
  BootstrapOptions worker_options = options;
  worker_options.approved_config_sha256 = config_sha256;
  worker_options.approved_release_index_sha256 =
      approved_payload.release_index_sha256;
  worker_options.approved_release_sequence = approved_payload.release_sequence;
#if defined(_WIN32)
  if (options.role == Role::Server)
    worker_options.quiet_status = true;
#endif
  const std::vector<std::string> invocation =
      BuildWorkerInvocation(worker_options, true);
  args.insert(args.end(), invocation.begin(), invocation.end());
  const bool background = options.role == Role::Client;
#if defined(_WIN32)
  if (options.role == Role::Server) {
    LaunchWindowsProcess(options.install_root, args, false, true);
    BootstrapTrace("LaunchApprovedUpdateWorker spawned temp worker native "
                   "new_process_group=1");
    return 0;
  }
#endif
  SDL_Process *process =
      SpawnProcess(options.install_root, std::move(args), {}, background);
  BootstrapTrace("LaunchApprovedUpdateWorker spawned temp worker background=" +
                 std::to_string(background ? 1 : 0));
  SDL_DestroyProcess(process);
  return 0;
}

int LaunchProtectedElevatedWorker(const BootstrapOptions &options) {
#if defined(_WIN32)
  if (!IsProcessElevated() || !options.worker_mode ||
      (!options.approved_install && !options.recover_only) ||
      !options.stage_elevated_worker)
    throw std::runtime_error("Invalid protected elevated-worker handoff");

  const fs::path canonical_root = fs::canonical(options.install_root);
  ValidatePrivilegedWorkerIdentity(options, kUpdaterStem,
                                   options.approved_install);
  AnchoredFileTree installed_files(canonical_root);
  installed_files.EnsurePrivateDirectory(".worr-update");
  if (!installed_files.ReplaceFileFrom(installed_files, kUpdaterStem,
                                       kElevatedWorkerPath))
    throw std::runtime_error(
        "Could not stage the protected elevated updater worker");

  BootstrapOptions child_options = options;
  child_options.install_root = canonical_root;
  child_options.worker_exe_path = canonical_root / kElevatedWorkerPath;
  child_options.stage_elevated_worker = false;
  child_options.wait_for_process_id = GetCurrentProcessId();
  child_options.wait_for_process_creation_time =
      CurrentProcessCreationTime();
  // The privileged child never launches the game. Do not carry caller-owned
  // command-line paths into its config/font/UI parsing boundary.
  child_options.forwarded_args.clear();
  child_options.quiet_status = true;
  std::vector<std::string> args = {GenericPath(child_options.worker_exe_path)};
  const std::vector<std::string> invocation =
      BuildWorkerInvocation(child_options, child_options.approved_install);
  args.insert(args.end(), invocation.begin(), invocation.end());
  LaunchWindowsProcess(canonical_root, args, options.role == Role::Client,
                       true);
  BootstrapTrace("LaunchProtectedElevatedWorker spawned protected worker");
  return 0;
#else
  (void)options;
  throw std::runtime_error(
      "Protected elevated-worker handoff is only available on Windows");
#endif
}

int RelaunchInstalledBootstrap(const BootstrapOptions &options) {
  const fs::path launch_path =
      options.install_root / Utf8Path(options.launch_relpath);
  if (!fs::is_regular_file(launch_path))
    throw std::runtime_error("Bootstrap executable not found: " +
                             GenericPath(launch_path));

  std::vector<std::string> args;
  args.push_back(GenericPath(launch_path));
  args.push_back(kSkipUpdateCheckArg);
  args.insert(args.end(), options.forwarded_args.begin(),
              options.forwarded_args.end());

  const bool background = options.role == Role::Client;
#if defined(_WIN32)
  if (options.role == Role::Server) {
    args.push_back(kQuietStatusArg);
    LaunchWindowsProcess(options.install_root, args, false, true);
    return 0;
  }
#endif
  SDL_Process *process =
      SpawnProcess(options.install_root, std::move(args), {}, background);
  SDL_DestroyProcess(process);
  return 0;
}

void RevalidateCachedRemotePayload(RemotePayload *payload,
                                   const UpdaterConfig &config,
                                   const BootstrapOptions &options,
                                   bool require_current_freshness) {
  if (!payload || payload->raw_release_index.empty() ||
      payload->raw_update_manifest.empty() ||
      payload->raw_release_index.size() > kMaxHttpMetadataBytes ||
      payload->raw_update_manifest.size() > kMaxHttpMetadataBytes)
    throw std::runtime_error(
        "Cached update authentication material is missing or oversized");

  Json::Value index_json;
  std::vector<std::string> verified_key_ids;
  bool unsigned_development = false;
  if (config.allow_unsigned_development_index) {
    if (!payload->raw_release_signature.empty())
      throw std::runtime_error("Unsigned development cache unexpectedly "
                               "contains a signature envelope");
    index_json = ParseJsonString(payload->raw_release_index,
                                 "cached unsigned development release index");
    unsigned_development = true;
  } else {
    if (!config.release_trust_policy || payload->raw_release_signature.empty())
      throw std::runtime_error(
          "Cached update has no verifiable release signature");
    VerifiedReleaseIndex verified = VerifyReleaseIndexSignature(
        payload->raw_release_index, config.release_index_asset,
        payload->raw_release_signature, *config.release_trust_policy,
        config.repo, config.channel);
    index_json = std::move(verified.index);
    verified_key_ids = std::move(verified.verified_key_ids);
  }

  const std::string version = JsonString(index_json, "version");
  const std::string tag = JsonString(index_json, "tag");
  if (JsonUInt64(index_json, "schema_version", 0) != 3 || version.empty() ||
      tag.empty() || JsonString(index_json, "repo") != config.repo ||
      JsonString(index_json, "channel") != config.channel)
    throw std::runtime_error("Cached release index identity is invalid");
  const ReleaseIndexSecurityMetadata security =
      ValidateReleaseIndexSecurityMetadata(index_json,
                                           require_current_freshness);
  const std::string release_index_sha256 = Sha256Hex(
      reinterpret_cast<const uint8_t *>(payload->raw_release_index.data()),
      payload->raw_release_index.size());
  if (std::find(security.revoked_index_sha256s.begin(),
                security.revoked_index_sha256s.end(),
                release_index_sha256) != security.revoked_index_sha256s.end())
    throw std::runtime_error("Cached release index revokes itself");

  const Json::Value &targets = index_json["targets"];
  if (!targets.isArray())
    throw std::runtime_error("Cached release index targets are not an array");
  Json::Value selected_target;
  for (const Json::Value &target : targets) {
    if (JsonString(target, "platform_id") != CurrentPlatformId())
      continue;
    if (!selected_target.isNull())
      throw std::runtime_error(
          "Cached release index repeats the active platform target");
    selected_target = target;
  }
  if (selected_target.isNull())
    throw std::runtime_error(
        "Cached release index does not contain the active platform target");

  const std::string role_name = RoleToCString(options.role);
  const Json::Value &roles = selected_target["roles"];
  if (!roles.isObject() || !roles.isMember(role_name) ||
      !roles[role_name].isObject())
    throw std::runtime_error(
        "Cached release index does not contain the active role");
  const Json::Value &role_json = roles[role_name];
  const std::string manifest_name =
      RequireSafeUpdaterFileName(JsonString(role_json, "update_manifest_name"),
                                 "cached manifest asset name");
  const std::string package_name =
      RequireSafeUpdaterFileName(JsonString(role_json, "update_package_name"),
                                 "cached package asset name");
  const AssetIntegrity manifest_integrity = SignedTargetAssetIntegrity(
      selected_target, manifest_name, "manifest", role_name);
  const AssetIntegrity package_integrity = SignedTargetAssetIntegrity(
      selected_target, package_name, "archive", role_name);
  if (payload->raw_update_manifest.size() != manifest_integrity.size ||
      Sha256Hex(reinterpret_cast<const uint8_t *>(
                    payload->raw_update_manifest.data()),
                payload->raw_update_manifest.size()) !=
          manifest_integrity.sha256)
    throw std::runtime_error("Cached update manifest does not match the "
                             "authenticated release index");

  payload->version = version;
  payload->tag = tag;
  payload->role = role_name;
  payload->launch_exe = RequireSafeUpdaterPath(
      JsonStringAny(role_json, {"launch_exe", "launcher_exe"},
                    DefaultLaunchRelpath(options.role)),
      "cached launch path");
  payload->engine_library = RequireSafeUpdaterPath(
      JsonStringAny(role_json, {"engine_library", "runtime_exe"},
                    DefaultEngineLibraryRelpath(options.role)),
      "cached engine path");
  payload->update_manifest_name = manifest_name;
  payload->update_package_name = package_name;
  payload->local_manifest_name = RequireSafeUpdaterFileName(
      JsonString(role_json, "local_manifest_name", kLocalManifestName),
      "cached local manifest name");
  payload->release_index_name = config.release_index_asset;
  payload->release_index_sha256 = release_index_sha256;
  payload->verified_release_key_ids = std::move(verified_key_ids);
  payload->revoked_release_index_sha256s = security.revoked_index_sha256s;
  payload->release_expires_at_utc = security.expires_at_utc;
  payload->release_sequence = security.sequence;
  payload->unsigned_development_index = unsigned_development;
  payload->update_manifest_integrity = manifest_integrity;
  payload->update_package_integrity = package_integrity;
  payload->manifest_json =
      ParseJsonString(payload->raw_update_manifest, "cached update manifest");
  ValidateSignedManifestTargetIdentity(payload->manifest_json, index_json,
                                       selected_target);
  ValidateRemotePayloadManifest(*payload);
}

RemotePayload
LoadPendingUpdate(const AnchoredFileTree &install_files,
                  const UpdaterConfig &config, const BootstrapOptions &options,
                  bool *found,
                  const std::map<std::string, uint64_t> &observed_revocations,
                  std::string *rejection = nullptr) {
  *found = false;
  try {
    if (!install_files.HasRegularFile(kStateName))
      return {};
    const Json::Value root = ParseJsonString(
        install_files.ReadFile(kStateName, kMaxPendingStateBytes),
        "cached updater state");
    if (JsonUInt64(root, "schema_version", 0) != 2)
      throw std::runtime_error(
          "Cached updater state uses an unauthenticated legacy schema");
    auto pending = RemotePayloadFromJson(root["pending_update"]);
    if (!pending)
      return {};
    RevalidateCachedRemotePayload(&*pending, config, options);
    const auto revocation =
        observed_revocations.find(pending->release_index_sha256);
    if (revocation != observed_revocations.end() &&
        revocation->second >= pending->release_sequence)
      throw std::runtime_error(
          "Cached update was revoked by an authenticated release index");
    ValidateRemotePayloadContext(*pending, config, options);
    *found = true;
    return *pending;
  } catch (const std::exception &e) {
    if (rejection)
      *rejection = e.what();
    BootstrapTrace("Cached pending update rejected: " + std::string(e.what()));
    return {};
  }
}

void SaveState(const fs::path &state_path, const std::string &result,
               const std::optional<RemotePayload> &pending) {
  Json::Value root(Json::objectValue);
  root["schema_version"] = 2;
  root["last_result"] = result;
  if (result != "check_failed")
    root["last_successful_check_utc"] = NowUtcString();
  if (pending)
    root["pending_update"] = RemotePayloadToJson(*pending);
  try {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    AnchoredFileTree installed_files(fs::canonical(state_path.parent_path()));
    installed_files.ReplaceFileWithBytes(
        RequireSafeUpdaterFileName(state_path.filename().generic_u8string(),
                                   "updater state file name"),
        Json::writeString(writer, root), InstallFileWriteOptions{0644, false});
  } catch (const std::exception &error) {
    BootstrapTrace("Could not persist updater state: " +
                   std::string(error.what()));
  }
}

std::string BuildSyncPromptDetail(const InstallSyncPlan &plan,
                                  const InstallManifest &local_manifest,
                                  const RemotePayload &payload,
                                  const std::string &warning) {
  std::ostringstream detail;
  if (plan.version_change) {
    detail << "Version " << payload.version << " is ready to synchronize";
    if (!local_manifest.version.empty())
      detail << " from " << local_manifest.version;
    detail << ".";
  } else {
    detail << "The local install differs from the authoritative manifest and "
              "can be synchronized.";
  }

  detail << " Planned changes: " << DescribeInstallSyncPlan(plan) << ".";
  if (!warning.empty()) {
    detail << " ";
    if (plan.version_change)
      detail << "A newer build is already known locally.";
    else
      detail << "The current manifest is already cached locally.";
    detail << " " << warning;
  }
  return detail.str();
}

int RunBootstrapFlow(const BootstrapOptions &options) {
  BootstrapTrace(
      "RunBootstrapFlow start role=" +
      std::string(RoleToCString(options.role)) +
      " worker_mode=" + std::to_string(options.worker_mode ? 1 : 0) +
      " approved_install=" + std::to_string(options.approved_install ? 1 : 0) +
      " skip_update_check=" +
      std::to_string(options.skip_update_check ? 1 : 0) +
      " quiet_status=" + std::to_string(options.quiet_status ? 1 : 0) +
      " install_root=" + GenericPath(options.install_root));
#if defined(_WIN32)
  if (options.wait_for_process_id != 0)
    WaitForBootstrapProcessExit(options.wait_for_process_id,
                                options.wait_for_process_creation_time);
  if (options.stage_elevated_worker)
    return LaunchProtectedElevatedWorker(options);
  const bool elevated_worker = options.worker_mode && IsProcessElevated();
  if (elevated_worker) {
    if ((!options.approved_install && !options.recover_only) ||
        options.stage_elevated_worker)
      throw std::runtime_error("Invalid direct privileged updater invocation");
    ValidatePrivilegedWorkerIdentity(options, kElevatedWorkerPath,
                                     options.approved_install);
  }
#else
  const bool elevated_worker = options.worker_mode && geteuid() == 0;
#endif
  SessionShellWindowConfig session_shell_config;
  if (options.role == Role::Client && !options.quiet_status)
    session_shell_config = LoadClientSessionShellWindowConfig(
        options.install_root, options.forwarded_args);
  BootstrapTrace("RunBootstrapFlow create_ui");
  UiHandle ui(options.role, options.quiet_status,
              options.role == Role::Client ? &session_shell_config : nullptr);
  BootstrapTrace("RunBootstrapFlow ui_ready");
  ui->SetStatus("Preparing WORR", "Starting bootstrap updater.");
  BootstrapTrace("RunBootstrapFlow initial_status_set");

  try {
    const InstallTransactionRecoveryResult recovery =
        RecoverInstallTransaction(options.install_root);
    if (recovery != InstallTransactionRecoveryResult::NothingToRecover) {
      BootstrapTrace("RunBootstrapFlow recovered_interrupted_install result=" +
                     std::to_string(static_cast<int>(recovery)));
      ui->SetStatus(
          "Recovered interrupted update",
          "The previous installation transaction was recovered safely.");
    }
  } catch (const fs::filesystem_error &error) {
#if defined(_WIN32)
    if (!IsProcessElevated() && IsPermissionDeniedError(error.code())) {
      BootstrapTrace("RunBootstrapFlow recovery_elevation_requested");
      ui->SetStatus("Waiting for administrator approval",
                    "WORR needs elevation to recover an interrupted update.");
      BootstrapOptions recovery_options = options;
      recovery_options.recover_only = true;
      recovery_options.stage_elevated_worker = true;
      recovery_options.wait_for_process_id = GetCurrentProcessId();
      recovery_options.wait_for_process_creation_time =
          CurrentProcessCreationTime();
      const fs::path recovery_root = fs::canonical(options.install_root);
      AnchoredFileTree recovery_files(recovery_root);
      const std::string recovery_config =
          recovery_files.ReadFile(kConfigName, kMaxUpdaterConfigBytes);
      const std::string recovery_config_sha256 =
          Sha256Hex(reinterpret_cast<const uint8_t *>(recovery_config.data()),
                    recovery_config.size());
      recovery_options.install_root =
          RequireProtectedElevationRoot(recovery_root, recovery_config_sha256);
      const std::vector<std::string> args =
          BuildWorkerInvocation(recovery_options, false);
      if (!RelaunchElevated(recovery_options.install_root / kUpdaterStem, args,
                            recovery_options.install_root))
        throw std::runtime_error(
            "Failed to request elevated updater recovery access");
      return 0;
    }
#endif
    throw;
  }
  if (options.recover_only) {
    ui->SetStatus("Update recovery complete",
                  "Launch WORR again to start the recovered installation.");
    return 0;
  }

  if (options.skip_update_check) {
    BootstrapTrace("RunBootstrapFlow skip_update_check launch");
    ui->SetStatus("Launching WORR",
                  "Starting the installed build without another update check.");
    if (elevated_worker)
      return 0;
    return LaunchEngineAndWait(options.install_root, options.launch_relpath,
                               options.engine_library_relpath,
                               options.forwarded_args, &ui);
  }

  const fs::path state_path = options.install_root / kStateName;
  BootstrapTrace(
      "RunBootstrapFlow paths config=" +
      GenericPath(options.install_root / kConfigName) +
      " manifest=" + GenericPath(options.install_root / kLocalManifestName) +
      " state=" + GenericPath(state_path));

  AnchoredFileTree install_files(fs::canonical(options.install_root));
  const bool has_config = install_files.HasRegularFile(kConfigName);
  const bool has_local_manifest =
      install_files.HasRegularFile(kLocalManifestName);
  if (!has_config || !has_local_manifest) {
    if (options.approved_install)
      throw std::runtime_error(
          "Approved updater worker requires an authenticated installed "
          "configuration and manifest");
    BootstrapTrace("RunBootstrapFlow dev_build config_or_manifest_missing");
    ui->SetStatus(
        "Developer build detected",
        "Skipping update checks and launching the hosted engine directly.");
    if (elevated_worker)
      return 0;
    return LaunchEngineAndWait(options.install_root, options.launch_relpath,
                               options.engine_library_relpath,
                               options.forwarded_args, &ui);
  }

  BootstrapTrace("RunBootstrapFlow load_config");
  const std::string raw_config =
      install_files.ReadFile(kConfigName, kMaxUpdaterConfigBytes);
  const std::string config_sha256 = Sha256Hex(
      reinterpret_cast<const uint8_t *>(raw_config.data()), raw_config.size());
  const UpdaterConfig config = LoadUpdaterConfig(raw_config, options.role);
  if (options.approved_install &&
      (!options.worker_mode ||
       options.approved_config_sha256 != config_sha256 ||
       !IsSha256Hex(options.approved_release_index_sha256) ||
       options.approved_release_sequence == 0))
    throw std::runtime_error(
        "Approved updater worker configuration changed after user approval");
  BootstrapTrace("RunBootstrapFlow config_loaded repo=" + config.repo +
                 " channel=" + config.channel + " role=" + config.role +
                 " autolaunch=" + std::to_string(config.autolaunch ? 1 : 0));
  BootstrapTrace("RunBootstrapFlow load_local_manifest");
  const std::string raw_local_manifest =
      install_files.ReadFile(kLocalManifestName, kMaxLocalManifestBytes);
  const InstallManifest local_manifest =
      LoadInstallManifest(raw_local_manifest, config, options);
  BootstrapTrace("RunBootstrapFlow local_manifest_loaded version=" +
                 local_manifest.version + " role=" + local_manifest.role);

  std::optional<RemotePayload> available_update;
  std::optional<InstallSyncPlan> available_sync_plan;
  std::map<std::string, uint64_t> observed_release_revocations;
  std::string warning;

  try {
    const auto deadline =
        clock_type::now() + std::chrono::milliseconds(kDiscoveryBudgetMs);
    RemotePayload discovered =
        DiscoverRemotePayload(config, options.role, &*ui, deadline,
                              local_manifest, &observed_release_revocations);
    ValidateApprovedReleaseBinding(discovered, options);
    ValidateRemotePayloadContext(discovered, config, options);
    ValidateReleaseSequenceAgainstInstalled(discovered, local_manifest);
    const int version_compare =
        CompareSemver(local_manifest.version, discovered.version);
    const InstallSyncPlan sync_plan = BuildInstallSyncPlan(
        options.install_root, local_manifest, config, discovered);
    BootstrapTrace("RunBootstrapFlow discovered version=" + discovered.version +
                   " version_compare=" + std::to_string(version_compare) +
                   " sync=" + std::to_string(sync_plan.RequiresSync() ? 1 : 0) +
                   " plan=\"" + DescribeInstallSyncPlan(sync_plan) + "\"");
    if (!discovered.version.empty() &&
        (version_compare < 0 ||
         (version_compare == 0 && sync_plan.RequiresSync()))) {
      available_update = discovered;
      available_sync_plan = sync_plan;
      const std::string state_result =
          sync_plan.version_change ? "update_available" : "sync_required";
      BootstrapTrace("RunBootstrapFlow update_available version=" +
                     discovered.version + " state=" + state_result);
      SaveState(state_path, state_result, available_update);
    } else {
      BootstrapTrace("RunBootstrapFlow current version=" +
                     local_manifest.version);
      SaveState(state_path, "current", std::nullopt);
    }
  } catch (const std::exception &e) {
    bool pending_found = false;
    std::string pending_rejection;
    RemotePayload pending =
        LoadPendingUpdate(install_files, config, options, &pending_found,
                          observed_release_revocations, &pending_rejection);
    if (pending_found)
      available_update = pending;
    else
      SaveState(state_path, "check_failed", std::nullopt);
    BootstrapTrace(std::string("RunBootstrapFlow discovery_warning pending=") +
                   std::to_string(pending_found ? 1 : 0) +
                   " error=" + e.what());
    warning = e.what();
    if (!pending_rejection.empty())
      warning += " Cached update ignored: " + pending_rejection + ".";
  }

  if (available_update) {
    try {
      ValidateApprovedReleaseBinding(*available_update, options);
      ValidateReleaseSequenceAgainstInstalled(*available_update,
                                              local_manifest);
    } catch (const std::exception &error) {
      warning += std::string(warning.empty() ? "" : " ") +
                 "Cached update ignored: " + error.what() + ".";
      available_update.reset();
      available_sync_plan.reset();
    }
  }

  if (available_update) {
    const int version_compare =
        CompareSemver(local_manifest.version, available_update->version);
    if (!available_sync_plan)
      available_sync_plan = BuildInstallSyncPlan(
          options.install_root, local_manifest, config, *available_update);
    if (version_compare > 0 || !available_sync_plan->RequiresSync()) {
      BootstrapTrace("RunBootstrapFlow stale_pending_sync ignored");
      available_update.reset();
      available_sync_plan.reset();
    }
  }

  if (available_update && available_sync_plan) {
    const std::string prompt_headline = available_sync_plan->version_change
                                            ? "Update required"
                                            : "Repair required";
    const std::string detail = BuildSyncPromptDetail(
        *available_sync_plan, local_manifest, *available_update, warning);
    const bool install =
        options.approved_install || ui->PromptInstall(prompt_headline, detail);
    if (!install) {
      BootstrapTrace("RunBootstrapFlow update_deferred");
      ui->SetStatus("Update deferred",
                    "Launching the installed build without synchronizing it.");
    } else {
      RevalidateRemotePayloadAtUse(&*available_update, config, options,
                                   local_manifest);
      const bool apply_in_process =
          CanApplySyncInProcess(options, *available_sync_plan);
      BootstrapTrace("RunBootstrapFlow apply_in_process=" +
                     std::to_string(apply_in_process ? 1 : 0));
      if (!apply_in_process) {
        BootstrapTrace("RunBootstrapFlow launch_temp_worker");
        ui->SetStatus(
            "Restarting updater",
            "Launching a temporary updater worker to synchronize the install.");
        return LaunchApprovedUpdateWorker(options, config_sha256,
                                          *available_update, local_manifest);
      }

      fs::path temp_root;
      bool protected_staging = false;
      if (elevated_worker) {
        const std::string protected_relative =
            ".worr-update/staging-" + RandomToken();
        install_files.EnsurePrivateDirectory(protected_relative);
        temp_root = options.install_root / Utf8Path(protected_relative);
        protected_staging = true;
      } else {
        temp_root = fs::temp_directory_path() /
                    ("worr-bootstrap-" + RandomToken());
      }
      const fs::path archive_path =
          temp_root / available_update->update_package_name;
      const fs::path stage_dir = temp_root / "stage";
      if (!protected_staging && !fs::create_directory(temp_root))
        throw std::runtime_error(
            "Could not create a private update staging directory");
      struct UpdateStagingCleanup {
        fs::path path;
        ~UpdateStagingCleanup() {
          std::error_code ignored;
          fs::remove_all(path, ignored);
        }
      } staging_cleanup{temp_root};
#if !defined(_WIN32)
      fs::permissions(temp_root, fs::perms::owner_all,
                      fs::perm_options::replace);
#endif
      if (!fs::create_directory(stage_dir))
        throw std::runtime_error(
            "Could not create the update extraction directory");

      if (available_sync_plan->RequiresPackagePayload()) {
        BootstrapTrace("RunBootstrapFlow worker_download_start");
        ui->SetStatus(
            "Downloading update",
            "Fetching the package needed to synchronize the install.");
        const int package_timeout = 30 * 60 * 1000;
        HttpDownloadFile(available_update->package_url, archive_path,
                         available_update->update_package_integrity.size,
                         package_timeout, &*ui, "Downloading update package");

        const Json::Value package = available_update->manifest_json["package"];
        const std::string package_hash = JsonString(package, "sha256");
        if (!package_hash.empty() &&
            ToLower(Sha256File(archive_path)) != ToLower(package_hash))
          throw std::runtime_error("Downloaded package hash mismatch");

        BootstrapTrace("RunBootstrapFlow worker_extract_start root=" +
                       GenericPath(temp_root));
        ExtractZip(archive_path, stage_dir, *available_update, &*ui);
        ValidateStagedPayload(stage_dir, *available_sync_plan);
      } else {
        BootstrapTrace("RunBootstrapFlow worker_metadata_only_sync");
      }
#if defined(_WIN32)
      RevalidateRemotePayloadAtUse(&*available_update, config, options,
                                   local_manifest);
      bool applied = false;
      for (int attempt = 0; attempt < kApplyRetryCount; ++attempt) {
        try {
          BootstrapTrace("RunBootstrapFlow apply_attempt=" +
                         std::to_string(attempt + 1));
          ApplyInstallSyncPlan(stage_dir, options.install_root,
                               *available_sync_plan, *available_update,
                               raw_local_manifest, &*ui);
          applied = true;
          BootstrapTrace("RunBootstrapFlow apply_success");
          break;
        } catch (const fs::filesystem_error &e) {
          BootstrapTrace(
              std::string("RunBootstrapFlow apply_filesystem_error code=") +
              std::to_string(e.code().value()) + " message=" + e.what());
          if (!IsPermissionDeniedError(e.code()))
            throw;
          if (attempt + 1 < kApplyRetryCount) {
            ui->SetStatus("Waiting for launcher shutdown",
                          "Finishing the previous process handoff before "
                          "synchronizing the install.");
            SDL_Delay(kApplyRetryDelayMs);
            continue;
          }
          if (!IsProcessElevated()) {
            BootstrapTrace("RunBootstrapFlow elevation_requested");
            ui->SetStatus("Waiting for administrator approval",
                          "WORR needs elevation to synchronize this install.");
            BootstrapOptions elevation_options = options;
            elevation_options.approved_config_sha256 = config_sha256;
            elevation_options.stage_elevated_worker = true;
            elevation_options.install_root = RequireProtectedElevationRoot(
                options.install_root, config_sha256, &local_manifest);
            std::vector<std::string> args =
                BuildWorkerInvocation(elevation_options, true);
            // Never elevate the temporary worker copy: its path lives in the
            // user's writable temporary directory and can be replaced by a
            // sibling process after this worker starts. The installed worker
            // is under the protected root just verified above.
            const fs::path elevation_worker =
                elevation_options.install_root / kUpdaterStem;
            if (!RelaunchElevated(elevation_worker, args,
                                  elevation_options.install_root))
              throw std::runtime_error(
                  "Failed to request elevated updater access");
            return 0;
          }
          throw;
        }
      }
      if (!applied)
        throw std::runtime_error("Failed applying update payload");
#else
      RevalidateRemotePayloadAtUse(&*available_update, config, options,
                                   local_manifest);
      ApplyInstallSyncPlan(stage_dir, options.install_root,
                           *available_sync_plan, *available_update,
                           raw_local_manifest, &*ui);
#endif
      BootstrapTrace("RunBootstrapFlow save_state_current");
      SaveState(state_path, "current", std::nullopt);

      const bool version_change = available_sync_plan->version_change;
      if (elevated_worker) {
        BootstrapTrace("RunBootstrapFlow elevated_worker_exit_after_apply");
        ui->SetStatus(
            version_change ? "Update installed" : "Repair complete",
            "The protected update completed. Launch WORR again to continue.");
        return 0;
      }
      if (!options.worker_mode) {
        BootstrapTrace("RunBootstrapFlow in_process_sync_launch_engine");
        ui->SetStatus(version_change ? "Update installed" : "Repair complete",
                      version_change
                          ? "Launching the updated hosted engine in the "
                            "current bootstrap shell."
                          : "Launching the synchronized hosted engine in the "
                            "current bootstrap shell.");
        return LaunchEngineAndWait(options.install_root, options.launch_relpath,
                                   options.engine_library_relpath,
                                   options.forwarded_args, &ui);
      }

      if (!config.autolaunch) {
        BootstrapTrace("RunBootstrapFlow update_installed_no_autolaunch");
        ui->SetStatus(version_change ? "Update installed" : "Repair complete",
                      version_change
                          ? "WORR was updated successfully. Launch it again to "
                            "start the new build."
                          : "The local install was synchronized successfully. "
                            "Launch WORR again to start.");
        return 0;
      }

      BootstrapTrace("RunBootstrapFlow relaunch_updated_bootstrap");
      ui->SetStatus(
          version_change ? "Update installed" : "Repair complete",
          version_change
              ? "Restarting the public WORR bootstrap with the updated build."
              : "Restarting the public WORR bootstrap with the synchronized "
                "install.");
      return RelaunchInstalledBootstrap(options);
    }
  } else if (!warning.empty()) {
    BootstrapTrace("RunBootstrapFlow warning_launch_installed error=" +
                   warning);
    ui->SetStatus("Update check unavailable",
                  warning + " Launching the installed build.");
  } else {
    BootstrapTrace("RunBootstrapFlow no_update_required");
    ui->SetStatus("WORR is current", "No update was required.");
  }

  BootstrapTrace("RunBootstrapFlow launch_installed_engine");
  if (elevated_worker) {
    ui->SetStatus("Updater finished",
                  "The elevated updater will not launch game code. Launch "
                  "WORR again to continue.");
    return 0;
  }
  return LaunchEngineAndWait(options.install_root, options.launch_relpath,
                             options.engine_library_relpath,
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
    if (arg == "--approved-config-sha256" && i + 1 < argc) {
      options.approved_config_sha256 = argv[++i];
      continue;
    }
    if (arg == "--approved-release-index-sha256" && i + 1 < argc) {
      options.approved_release_index_sha256 = argv[++i];
      continue;
    }
    if (arg == "--approved-release-sequence" && i + 1 < argc) {
      if (!ParseUnsigned(argv[++i], &options.approved_release_sequence) ||
          options.approved_release_sequence == 0)
        throw std::runtime_error("Invalid approved release sequence");
      continue;
    }
    if (arg == "--stage-elevated-worker") {
      options.stage_elevated_worker = true;
      continue;
    }
    if (arg == "--wait-for-process" && i + 1 < argc) {
      uint64_t process_id = 0;
      if (!ParseUnsigned(argv[++i], &process_id) || process_id == 0 ||
          process_id > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Invalid updater handoff process identity");
      options.wait_for_process_id = static_cast<uint32_t>(process_id);
      continue;
    }
    if (arg == "--wait-for-process-creation-time" && i + 1 < argc) {
      if (!ParseUnsigned(argv[++i],
                         &options.wait_for_process_creation_time) ||
          options.wait_for_process_creation_time == 0)
        throw std::runtime_error("Invalid updater handoff process identity");
      continue;
    }
    if (arg == "--recover-only") {
      options.recover_only = true;
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
  if ((options.wait_for_process_id == 0) !=
      (options.wait_for_process_creation_time == 0))
    throw std::runtime_error("Incomplete updater handoff process identity");
  options.quiet_status = options.quiet_status ||
                         ForwardedArgsRequestHeadless(options.forwarded_args);
  BootstrapTrace(
      "ParseBootstrapOptions role=" + std::string(RoleToCString(options.role)) +
      " approved_install=" + std::to_string(options.approved_install ? 1 : 0) +
      " skip_update_check=" +
      std::to_string(options.skip_update_check ? 1 : 0) +
      " quiet_status=" + std::to_string(options.quiet_status ? 1 : 0) +
      " forwarded_argc=" + std::to_string(options.forwarded_args.size()));
  return options;
}

} // namespace

const char *RoleToCString(Role role) {
  return role == Role::Client ? "client" : "server";
}

int RunLauncher(Role role, const std::string &launch_relative_path,
                const std::string &engine_library_relative_path, int argc,
                char **argv) {
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
    options.quiet_status = options.quiet_status ||
                           ForwardedArgsRequestHeadless(options.forwarded_args);
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
                    const std::string &engine_library_relative_path, int argc,
                    wchar_t **argv) {
  std::vector<std::string> utf8_args;
  utf8_args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i)
    utf8_args.push_back(WideToUtf8(argv[i]));

  std::vector<char *> raw_args;
  raw_args.reserve(utf8_args.size() + 1);
  for (std::string &arg : utf8_args)
    raw_args.push_back(arg.data());
  raw_args.push_back(nullptr);

  return RunLauncher(role, launch_relative_path, engine_library_relative_path,
                     argc, raw_args.data());
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
