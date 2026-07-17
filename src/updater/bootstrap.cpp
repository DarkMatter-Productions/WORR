#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "bootstrap_shared.h"
#include "common/bootstrap.h"

#include <SDL3/SDL.h>
#if USE_SDL3_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif
#include <curl/curl.h>
#include <json/json.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <limits>
#include <optional>
#include <random>
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
constexpr const char *kBootstrapWin32HwndEnv = "WORR_BOOTSTRAP_WIN32_HWND";
constexpr const char *kBootstrapTransitionEnv = "WORR_BOOTSTRAP_TRANSITION";
constexpr const char *kEngineEntryPoint = "WORR_EngineMain";
constexpr const char *kReadyCallbackEntryPoint = "Com_SetBootstrapReadyCallback";
constexpr const char *kSkipUpdateCheckArg = "--bootstrap-skip-update-check";
constexpr const char *kQuietStatusArg = "--bootstrap-quiet-status";
constexpr int kConnectTimeoutMs = 2000;
constexpr int kDiscoveryBudgetMs = 5000;
constexpr int kApplyRetryCount = 20;
constexpr int kApplyRetryDelayMs = 100;
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

  bool RequiresSync() const { return metadata_change || !install_actions.empty() || !remove_paths.empty(); }
  bool RequiresPackagePayload() const { return !install_actions.empty(); }
  uint64_t TotalApplySteps() const {
    return static_cast<uint64_t>(install_actions.size()) + static_cast<uint64_t>(remove_paths.size());
  }

  uint64_t AddCount() const {
    return static_cast<uint64_t>(std::count_if(install_actions.begin(), install_actions.end(), [](const InstallAction &a) {
      return a.kind == InstallActionKind::Add;
    }));
  }

  uint64_t RefreshCount() const {
    return static_cast<uint64_t>(std::count_if(install_actions.begin(), install_actions.end(),
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

std::string PointerString(const void *pointer) {
  std::ostringstream ss;
  ss << std::hex << reinterpret_cast<uintptr_t>(pointer);
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

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
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
  if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
    return false;

  *out = static_cast<int>(value);
  return true;
}

std::string StripMatchingQuotes(std::string text) {
  text = Trim(std::move(text));
  if (text.size() >= 2 &&
      ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
    text = text.substr(1, text.size() - 2);
  }
  return text;
}

bool ParseGeometryString(const std::string &text, int *x, int *y, int *width, int *height) {
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
  if (parsed_width < 1 || parsed_width > 8192 || parsed_height < 1 || parsed_height > 8192)
    return false;
  if (parsed_x < std::numeric_limits<int>::min() || parsed_x > std::numeric_limits<int>::max() ||
      parsed_y < std::numeric_limits<int>::min() || parsed_y > std::numeric_limits<int>::max())
    return false;

  *x = static_cast<int>(parsed_x);
  *y = static_cast<int>(parsed_y);
  *width = static_cast<int>(parsed_width);
  *height = static_cast<int>(parsed_height);
  return true;
}

bool IsSetCommand(std::string_view token) {
  return EqualsIgnoreCase(token, "+set") || EqualsIgnoreCase(token, "+seta") || EqualsIgnoreCase(token, "+sets") ||
         EqualsIgnoreCase(token, "+setu") || EqualsIgnoreCase(token, "+setr");
}

void OverlayCvarsFromCommandLine(const std::vector<std::string> &args, std::map<std::string, std::string> *cvars) {
  if (!cvars)
    return;

  for (size_t i = 0; i < args.size(); ++i) {
    if (!IsSetCommand(args[i]) || i + 2 >= args.size())
      continue;
    (*cvars)[ToLower(args[i + 1])] = args[i + 2];
    i += 2;
  }
}

void OverlayCvarsFromFile(const fs::path &path, std::map<std::string, std::string> *cvars) {
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
      while (cursor < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[cursor])))
        ++cursor;
      if (cursor >= trimmed.size())
        return false;
      const size_t start = cursor;
      while (cursor < trimmed.size() && !std::isspace(static_cast<unsigned char>(trimmed[cursor])))
        ++cursor;
      *token = trimmed.substr(start, cursor - start);
      return true;
    };

    std::string command;
    std::string name;
    if (!next_token(&command) || !next_token(&name))
      continue;

    const std::string lowered = ToLower(command);
    if (lowered != "set" && lowered != "seta" && lowered != "sets" && lowered != "setu" && lowered != "setr")
      continue;

    while (cursor < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[cursor])))
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
  if (!std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c); }))
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

fs::path ResolveClientRuntimeRoot(const fs::path &install_root, const std::map<std::string, std::string> &cli_cvars) {
  const auto it = cli_cvars.find("basedir");
  if (it == cli_cvars.end() || it->second.empty())
    return install_root;

  fs::path runtime_root = Utf8Path(it->second);
  if (!runtime_root.is_absolute())
    runtime_root = install_root / runtime_root;
  return NormalizeInstallRoot(runtime_root);
}

SessionShellWindowConfig LoadClientSessionShellWindowConfig(const fs::path &install_root,
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
    if (ParseGeometryString(geometry_it->second, &parsed_x, &parsed_y, &parsed_width, &parsed_height)) {
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
  if (const auto it = cvars.find("win_fullscreen_capture_friendly"); it != cvars.end()) {
    int value = 1;
    if (ParseIntStrict(it->second, &value))
      config.fullscreen_capture_friendly = value != 0;
  }
  if (const auto it = cvars.find("r_display"); it != cvars.end() && !it->second.empty())
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

  BootstrapTrace("LoadClientSessionShellWindowConfig root=" + GenericPath(config.runtime_root) +
                 " mode=" + SessionShellWindowModeToCString(config.mode) +
                 " fullscreen_index=" + std::to_string(config.fullscreen_index) +
                 " borderless=" + std::to_string(config.borderless) +
                 " exclusive=" + std::to_string(config.fullscreen_exclusive ? 1 : 0) +
                 " capture_friendly=" + std::to_string(config.fullscreen_capture_friendly ? 1 : 0) +
                 " monitor_mode=" + std::to_string(config.monitor_mode) + " display=\"" + config.display_request +
                 "\" geometry=" + geometry_detail.str());
  return config;
}

SDL_DisplayID ResolveSessionShellDisplayId(const SessionShellWindowConfig &config, bool *invalid) {
  if (invalid)
    *invalid = false;

  if (config.monitor_mode != 1 || config.display_request.empty() || config.display_request == "0")
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
  if (const std::optional<int> ordinal = ParseDisplayOrdinalHint(config.display_request); ordinal.has_value()) {
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

SessionShellWindowPlacement ResolveSessionShellPlacement(const SessionShellWindowConfig &config) {
  SessionShellWindowPlacement placement;
  placement.width = config.width;
  placement.height = config.height;
  placement.x = config.x;
  placement.y = config.y;
  placement.display_id = ResolveSessionShellDisplayId(config, &placement.invalid_display);

  if (config.mode == SessionShellWindowMode::SpanBorderless) {
    placement.has_bounds = ResolveSessionShellSpanBounds(&placement.bounds);
  } else if (placement.display_id) {
    placement.has_bounds = SDL_GetDisplayBounds(placement.display_id, &placement.bounds);
  }

  const bool fullscreen = config.mode == SessionShellWindowMode::BorderlessFullscreen ||
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

std::optional<SDL_DisplayMode> ResolveExclusiveDisplayMode(SDL_DisplayID display_id, int fullscreen_index) {
  if (!display_id)
    return std::nullopt;

  if (fullscreen_index <= 1) {
    const SDL_DisplayMode *desktop_mode = SDL_GetDesktopDisplayMode(display_id);
    if (!desktop_mode)
      return std::nullopt;
    return *desktop_mode;
  }

  int num_modes = 0;
  SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display_id, &num_modes);
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
  virtual bool WaitForMinimumDisplayTime() { return true; }
  virtual void DismissForEngineHandoff() = 0;
  virtual bool SupportsSharedWindowHandoff() const { return false; }
  virtual bool PrepareSharedWindowHandoff() { return false; }
  virtual void *GetSharedWindowNativeHandle() { return nullptr; }
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

class SplashUi final : public BootstrapUi {
public:
  explicit SplashUi(SessionShellWindowConfig config) : config_(std::move(config)) {
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

    InitializeTextRenderer();
  }

  ~SplashUi() override { ReleaseUiResources(false); }

  bool SetStatus(const std::string &headline, const std::string &detail = {}) override {
    headline_ = headline;
    detail_ = detail;
    Render();
    return !closed_;
  }

  bool SetProgress(const std::string &label, uint64_t current, uint64_t total) override {
    const bool label_changed = progress_label_ != label;
    progress_label_ = label;
    progress_current_ = current;
    progress_total_ = total;
    if (progress_total_ > 0) {
      const float target = std::clamp(static_cast<float>(progress_current_) / static_cast<float>(progress_total_), 0.0f,
                                      1.0f);
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
    AnimateProgress();
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
    // swapchain owner. Keeping it as the final app HWND leaves Windows 11 taskbar
    // thumbnails and OS PrintScreen sampling stale bootstrap pixels, so let the
    // engine create and own its real window for now.
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
      "Quake, Quake II, id Software, Bethesda, and related marks are the property of id Software LLC and/or ZeniMax Media Inc.\n"
      "WORR is an unofficial fan project and is not affiliated with or endorsed by id Software or Bethesda.";

#if defined(_WIN32)
  static void FlushDesktopComposition() {
    GdiFlush();

    using dwm_flush_fn = HRESULT(WINAPI *)(void);
    static HMODULE dwmapi = LoadLibraryA("dwmapi.dll");
    static auto dwm_flush = dwmapi ? reinterpret_cast<dwm_flush_fn>(GetProcAddress(dwmapi, "DwmFlush")) : nullptr;
    if (dwm_flush)
      dwm_flush();
  }

  static LRESULT CALLBACK NativeShellWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    default:
      break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
  }

  static const char *NativeShellWindowClassName() { return "WORRBootstrapShellWindow"; }

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
      throw std::runtime_error("Failed registering bootstrap shell window class");

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

    const bool fullscreen = config_.mode == SessionShellWindowMode::BorderlessFullscreen ||
                            config_.mode == SessionShellWindowMode::ExclusiveFullscreen ||
                            config_.mode == SessionShellWindowMode::SpanBorderless;
    const bool borderless = fullscreen || config_.mode == SessionShellWindowMode::BorderlessWindow;
    DWORD style = borderless ? (WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
                             : (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    DWORD exstyle = WS_EX_TOOLWINDOW;

    RECT monitor_rect = ResolveMonitorRect(fullscreen);
    int client_width = fullscreen ? (monitor_rect.right - monitor_rect.left) : placement_.width;
    int client_height = fullscreen ? (monitor_rect.bottom - monitor_rect.top) : placement_.height;
    int x = fullscreen ? monitor_rect.left : placement_.x;
    int y = fullscreen ? monitor_rect.top : placement_.y;

    if (!fullscreen) {
      if (SDL_WINDOWPOS_ISCENTERED(x))
        x = monitor_rect.left + ((monitor_rect.right - monitor_rect.left) - client_width) / 2;
      if (SDL_WINDOWPOS_ISCENTERED(y))
        y = monitor_rect.top + ((monitor_rect.bottom - monitor_rect.top) - client_height) / 2;
    }

    RECT rect{0, 0, client_width, client_height};
    AdjustWindowRectEx(&rect, style, FALSE, exstyle);
    const int window_width = rect.right - rect.left;
    const int window_height = rect.bottom - rect.top;

    SetWindowLongPtrA(native_window_, GWL_STYLE, static_cast<LONG_PTR>(style));
    SetWindowLongPtrA(native_window_, GWL_EXSTYLE, static_cast<LONG_PTR>(exstyle));
    SetWindowPos(native_window_, fullscreen ? HWND_TOP : HWND_NOTOPMOST, x, y, window_width, window_height,
                 SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER | SWP_HIDEWINDOW);
    UpdateWindow(native_window_);
  }

  void CreateNativeShellWindow() {
    EnsureNativeShellWindowClass();

    native_window_ = CreateWindowExA(WS_EX_TOOLWINDOW, NativeShellWindowClassName(), "WORR",
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
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, native_window_);

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
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,
                           config_.mode == SessionShellWindowMode::Windowed);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, placement_.width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, placement_.height);
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
      BootstrapTrace("SplashUi step=" + std::string(step) + " error=" + SDL_GetError());
      return false;
    }
    return true;
  }

  bool ApplyWindowedMode(bool bordered = true) {
    bool ok = true;
    ok &= ApplySdlChange(SDL_SetWindowFullscreen(window_, false), "leave_fullscreen");
    ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, nullptr), "clear_fullscreen_mode");
    ok &= ApplySdlChange(SDL_SetWindowBordered(window_, bordered), bordered ? "set_bordered" : "set_borderless");
    ok &= ApplySdlChange(SDL_SetWindowSize(window_, config_.width, config_.height), "set_window_size");
    if (config_.geometry_specified) {
      ok &= ApplySdlChange(SDL_SetWindowPosition(window_, config_.x, config_.y), "set_window_position");
    }
    ok &= ApplySdlChange(SDL_SyncWindow(window_), "sync_window");
    return ok;
  }

  bool ApplySpanBorderlessMode() {
    if (!placement_.has_bounds)
      return false;

    bool ok = true;
    ok &= ApplySdlChange(SDL_SetWindowFullscreen(window_, false), "leave_fullscreen");
    ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, nullptr), "clear_fullscreen_mode");
    ok &= ApplySdlChange(SDL_SetWindowBordered(window_, false), "set_borderless");
    ok &= ApplySdlChange(SDL_SetWindowPosition(window_, placement_.bounds.x, placement_.bounds.y), "set_span_position");
    ok &= ApplySdlChange(SDL_SetWindowSize(window_, placement_.bounds.w, placement_.bounds.h), "set_span_size");
    ok &= ApplySdlChange(SDL_SyncWindow(window_), "sync_window");
    return ok;
  }

  bool ApplyBorderlessFullscreenMode() {
    if (placement_.has_bounds) {
      ApplySdlChange(SDL_SetWindowPosition(window_, placement_.bounds.x, placement_.bounds.y), "set_fullscreen_position");
      ApplySdlChange(SDL_SetWindowSize(window_, placement_.bounds.w, placement_.bounds.h), "set_fullscreen_size");
    }

    bool ok = true;
    ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, nullptr), "clear_fullscreen_mode");
    ok &= ApplySdlChange(SDL_SetWindowFullscreen(window_, true), "enter_borderless_fullscreen");
    ok &= ApplySdlChange(SDL_SyncWindow(window_), "sync_window");
    return ok;
  }

  bool ApplyExclusiveFullscreenMode() {
    bool ok = true;
    if (placement_.has_bounds) {
      ok &= ApplySdlChange(SDL_SetWindowPosition(window_, placement_.bounds.x, placement_.bounds.y),
                           "set_exclusive_position");
    }

    const std::optional<SDL_DisplayMode> display_mode =
        ResolveExclusiveDisplayMode(placement_.display_id, config_.fullscreen_index);
    if (display_mode.has_value()) {
      ok &= ApplySdlChange(SDL_SetWindowSize(window_, display_mode->w, display_mode->h), "set_exclusive_size");
      ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, &*display_mode), "set_exclusive_mode");
    } else {
      if (placement_.has_bounds) {
        ok &= ApplySdlChange(SDL_SetWindowSize(window_, placement_.bounds.w, placement_.bounds.h),
                             "set_exclusive_size");
      }
      ok &= ApplySdlChange(SDL_SetWindowFullscreenMode(window_, nullptr), "clear_fullscreen_mode");
    }

    ok &= ApplySdlChange(SDL_SetWindowFullscreen(window_, true), "enter_exclusive_fullscreen");
    ok &= ApplySdlChange(SDL_SyncWindow(window_), "sync_window");
    return ok;
  }

  void ApplySessionShellWindowMode() {
#if defined(_WIN32)
    if (wrapped_external_window_) {
      ApplyNativeWindowMode();
      const char *display_name = placement_.display_id ? SDL_GetDisplayName(placement_.display_id) : nullptr;
      std::ostringstream detail;
      detail << "SplashUi window_mode=" << SessionShellWindowModeToCString(config_.mode)
             << " display_id=" << static_cast<unsigned long long>(placement_.display_id)
             << " invalid_display=" << (placement_.invalid_display ? 1 : 0)
             << " fallback=0"
             << " bounds=" << placement_.bounds.w << 'x' << placement_.bounds.h << std::showpos << placement_.bounds.x
             << placement_.bounds.y << std::noshowpos << " create=" << placement_.width << 'x' << placement_.height
             << " native_wrap=1";
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
      BootstrapTrace("SplashUi window_mode_fallback requested=" + std::string(SessionShellWindowModeToCString(config_.mode)));
      ApplyWindowedMode();
      fell_back = true;
    }

    const char *display_name = placement_.display_id ? SDL_GetDisplayName(placement_.display_id) : nullptr;
    std::ostringstream detail;
    detail << "SplashUi window_mode=" << SessionShellWindowModeToCString(config_.mode)
           << " display_id=" << static_cast<unsigned long long>(placement_.display_id)
           << " invalid_display=" << (placement_.invalid_display ? 1 : 0)
           << " fallback=" << (fell_back ? 1 : 0)
           << " bounds=" << placement_.bounds.w << 'x' << placement_.bounds.h << std::showpos << placement_.bounds.x
           << placement_.bounds.y << std::noshowpos << " create=" << placement_.width << 'x' << placement_.height;
    if (display_name && *display_name)
      detail << " name=\"" << display_name << '"';
    BootstrapTrace(detail.str());
  }

  void ReleaseUiResources(bool keep_video_subsystem, bool keep_native_window = false) {
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

  static float ResponsiveTextSize(float reference, float fraction, float min_px, float max_px) {
    return std::clamp(reference * fraction, min_px, max_px);
  }

  static float DebugTextScaleForSize(float pixel_size) {
    return pixel_size / static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
  }

  static float DebugTextPixelSize(float pixel_size) {
    return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * DebugTextScaleForSize(pixel_size);
  }

  static float DebugTextLineAdvance(float pixel_size) {
    return (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 4.0f) * DebugTextScaleForSize(pixel_size);
  }

  void DrawScaledDebugText(float x, float y, const std::string &text, float pixel_size) {
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
      candidates.push_back(config_.runtime_root / "basew" / "fonts" / "AtkinsonHyperLegible-Regular.otf");
      candidates.push_back(config_.runtime_root / "fonts" / "AtkinsonHyperLegible-Regular.otf");
    }

    if (const char *base_path = SDL_GetBasePath()) {
      fs::path base = Utf8Path(base_path);
      candidates.push_back(base / "basew" / "fonts" / "AtkinsonHyperLegible-Regular.otf");
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
    candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    candidates.push_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
#endif

    return candidates;
  }

  void InitializeTextRenderer() {
    if (!TTF_Init()) {
      BootstrapTrace(std::string("SplashUi ttf_init_failed error=") + SDL_GetError());
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

    TTF_Font *font = TTF_OpenFont(GenericPath(ttf_font_path_).c_str(), static_cast<float>(size));
    if (!font) {
      BootstrapTrace("SplashUi ttf_open_failed size=" + std::to_string(size) + " error=" + SDL_GetError());
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

  std::vector<std::string> WrapTextByWidth(const std::string &text, float pixel_size, float max_width) {
    std::vector<std::string> lines;
    std::stringstream paragraphs(text);
    std::string paragraph;

    while (std::getline(paragraphs, paragraph)) {
      std::istringstream words(paragraph);
      std::string word;
      std::string current;

      while (words >> word) {
        std::string candidate = current.empty() ? word : current + " " + word;
        if (current.empty() || MeasureTextLineWidth(candidate, pixel_size) <= max_width) {
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

  float MeasureTextBlockHeight(const std::string &text, float pixel_size, float max_width) {
    return static_cast<float>(WrapTextByWidth(text, pixel_size, max_width).size()) *
           TextLineAdvance(pixel_size);
  }

  void DrawTextLine(float x, float y, const std::string &line, float pixel_size, SDL_Color color) {
    if (line.empty())
      return;

#if USE_SDL3_TTF
    if (TTF_Font *font = GetTextFont(pixel_size)) {
      SDL_Surface *surface = TTF_RenderText_Blended(font, line.c_str(), line.size(), color);
      if (surface) {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
        if (texture) {
          SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
          SDL_FRect dst{x, y, static_cast<float>(surface->w), static_cast<float>(surface->h)};
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

  void DrawCenteredTextBlock(float center_x, float y, const std::string &text, float pixel_size,
                             float max_width, SDL_Color color) {
    float line_y = y;
    const float line_advance = TextLineAdvance(pixel_size);
    for (const std::string &line : WrapTextByWidth(text, pixel_size, max_width)) {
      const float line_width = MeasureTextLineWidth(line, pixel_size);
      DrawTextLine(center_x - (line_width * 0.5f), line_y, line, pixel_size, color);
      line_y += line_advance;
    }
  }

  float DrawCenteredTextMeasured(float center_x, float y, const std::string &text, float pixel_size,
                                 float max_width, SDL_Color color) {
    float line_y = y;
    const float line_advance = TextLineAdvance(pixel_size);
    for (const std::string &line : WrapTextByWidth(text, pixel_size, max_width)) {
      const float line_width = MeasureTextLineWidth(line, pixel_size);
      DrawTextLine(center_x - (line_width * 0.5f), line_y, line, pixel_size, color);
      line_y += line_advance;
    }
    return line_y;
  }

  void DrawButton(const SDL_FRect &rect, const std::string &label, float text_size, bool primary) {
    SDL_SetRenderDrawColor(renderer_, primary ? 159 : 56, primary ? 202 : 64, primary ? 68 : 56, 255);
    SDL_RenderFillRect(renderer_, &rect);
    SDL_SetRenderDrawColor(renderer_, 229, 235, 219, 255);
    SDL_RenderRect(renderer_, &rect);
    const float text_width = MeasureTextLineWidth(label, text_size);
    const float text_height = TextLineAdvance(text_size);
    DrawTextLine(rect.x + (rect.w - text_width) * 0.5f,
                 rect.y + (rect.h - text_height) * 0.5f,
                 label, text_size, Color(229, 235, 219));
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

  void DrawArc(float cx, float cy, float radius, float start, float end, int segments) {
    if (segments < 1)
      return;
    float last_x = cx + std::cos(start) * radius;
    float last_y = cy + std::sin(start) * radius;
    for (int i = 1; i <= segments; ++i) {
      const float t = start + (end - start) * (static_cast<float>(i) / static_cast<float>(segments));
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
        DrawArc(cx, cy, radius - static_cast<float>(i), start, end, std::max(1, static_cast<int>(ring_segments * progress)));

      const int percent = static_cast<int>(std::round(progress * 100.0f));
      const std::string value = std::to_string(percent) + "%";
      const float value_size = std::clamp(radius * 0.42f, 12.0f, 18.0f);
      const float value_width = MeasureTextLineWidth(value, value_size);
      DrawTextLine(cx - value_width * 0.5f, cy - TextLineAdvance(value_size) * 0.5f,
                   value, value_size, Color(229, 235, 219));
    } else {
      const float tick = static_cast<float>(SDL_GetTicks() % 1200u) / 1200.0f;
      const float start = -1.57079632679f + tau * tick;
      const float end = start + 1.35f;
      SDL_SetRenderDrawColor(renderer_, 155, 198, 66, 255);
      for (int i = 0; i < 5; ++i)
        DrawArc(cx, cy, radius - static_cast<float>(i), start, end, ring_segments / 4);
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
    const float headline_text_size =
        ResponsiveTextSize(size_reference, 0.026f, kHeadlineTextMinPx, kHeadlineTextMaxPx);
    const float detail_text_size =
        ResponsiveTextSize(size_reference, 0.018f, kDetailTextMinPx, kDetailTextMaxPx);
    const float progress_text_size =
        ResponsiveTextSize(size_reference, 0.016f, kProgressTextMinPx, kProgressTextMaxPx);
    const float button_text_size =
        ResponsiveTextSize(size_reference, 0.017f, kButtonTextMinPx, kButtonTextMaxPx);
    const float legal_text_size =
        ResponsiveTextSize(size_reference, 0.0125f, kLegalTextMinPx, kLegalTextMaxPx);
    const float virtual_43_width = std::min(static_cast<float>(width), static_cast<float>(height) * (4.0f / 3.0f));
    const float legal_margin = 12.0f;
    const float legal_width = std::min<float>(virtual_43_width, static_cast<float>(width) - 96.0f);
    const float legal_height = MeasureTextBlockHeight(kLegalText, legal_text_size, legal_width);
    const float footer_top = static_cast<float>(height) - legal_height - legal_margin;
    const float content_top = 24.0f;
    const float content_bottom = footer_top - 22.0f;
    const float available_height = std::max(0.0f, content_bottom - content_top);
    const float status_width = std::min<float>(virtual_43_width, static_cast<float>(width) - 140.0f);

    const bool has_progress = !progress_label_.empty();
    const bool has_headline = !headline_.empty();
    const bool has_detail = !detail_.empty();
    const float progress_height = has_progress ? MeasureTextBlockHeight(progress_label_, progress_text_size, status_width) : 0.0f;
    const float headline_height = has_headline ? MeasureTextBlockHeight(headline_, headline_text_size, status_width) : 0.0f;
    const float detail_height = has_detail ? MeasureTextBlockHeight(detail_, detail_text_size, status_width) : 0.0f;

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

    float spinner_radius = std::clamp(static_cast<float>(std::min(width, height)) * 0.055f, 26.0f, 46.0f);
    float spinner_block_height = spinner_radius * 2.0f;
    const float button_block_height = prompt_active_ ? (18.0f + kButtonHeight) : 0.0f;
    const float gap_logo_spinner = 20.0f;
    const float gap_spinner_text = 18.0f;
    const float gap_text_buttons = prompt_active_ ? 18.0f : 0.0f;
    const float minimum_banner_height = 96.0f;
    const float minimum_spinner_block = 48.0f;
    const float minimum_stack_height = minimum_banner_height + minimum_spinner_block + text_block_height +
                                       button_block_height + gap_logo_spinner + gap_spinner_text + gap_text_buttons;
    if (minimum_stack_height > available_height && spinner_block_height > minimum_spinner_block) {
      const float overflow = minimum_stack_height - available_height;
      const float shrink = std::min((spinner_block_height - minimum_spinner_block) * 0.5f, overflow * 0.5f);
      spinner_radius = std::max(24.0f, spinner_radius - shrink);
      spinner_block_height = spinner_radius * 2.0f;
    }

    const float banner_aspect = static_cast<float>(generated::kBootstrapLogoHeight) /
                                static_cast<float>(generated::kBootstrapLogoWidth);
    float banner_width = virtual_43_width;
    float banner_height = banner_width * banner_aspect;
    const float max_banner_height = std::max(minimum_banner_height,
                                             available_height - spinner_block_height - text_block_height -
                                                 button_block_height - gap_logo_spinner - gap_spinner_text -
                                                 gap_text_buttons);
    const float preferred_banner_height = std::min(max_banner_height, static_cast<float>(height) * 0.34f);
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

    const float stack_height = banner_height + spinner_block_height + text_block_height + button_block_height +
                               gap_logo_spinner + gap_spinner_text + gap_text_buttons;
    const float stack_top = content_top + std::max(0.0f, (available_height - stack_height) * 0.35f);
    SDL_FRect banner_rect{center_x - banner_width * 0.5f, stack_top, banner_width, banner_height};
    SDL_RenderTexture(renderer_, logo_, nullptr, &banner_rect);

    const float spinner_y = banner_rect.y + banner_rect.h + gap_logo_spinner + spinner_radius;
    DrawCircularIndicator(center_x, spinner_y, spinner_radius);

    float text_y = spinner_y + spinner_radius + gap_spinner_text;
    if (has_progress) {
      text_y = DrawCenteredTextMeasured(center_x, text_y, progress_label_, progress_text_size,
                                        status_width, Color(155, 198, 66));
      text_y += 4.0f;
    }
    if (has_headline) {
      text_y = DrawCenteredTextMeasured(center_x, text_y, headline_, headline_text_size,
                                        status_width, Color(229, 235, 219));
    }
    if (has_detail) {
      if (has_headline || has_progress)
        text_y += 4.0f;
      text_y = DrawCenteredTextMeasured(center_x, text_y, detail_, detail_text_size,
                                        status_width, Color(170, 179, 163));
    }

    if (prompt_active_) {
      const float total_button_width = (kButtonWidth * 2.0f) + kButtonGap;
      const float button_y = text_y + gap_text_buttons;
      install_button_ = SDL_FRect{center_x - total_button_width * 0.5f, button_y, kButtonWidth, kButtonHeight};
      exit_button_ = SDL_FRect{install_button_.x + kButtonWidth + kButtonGap, button_y, kButtonWidth, kButtonHeight};
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
  UiHandle(Role role, bool quiet_status, const SessionShellWindowConfig *session_shell_config = nullptr) {
    if (quiet_status) {
      ui_ = std::make_unique<SilentUi>();
      return;
    }
    if (role == Role::Client) {
      try {
        ui_ = std::make_unique<SplashUi>(session_shell_config ? *session_shell_config : SessionShellWindowConfig{});
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

RemotePayload DiscoverRemotePayload(const UpdaterConfig &config, Role role, BootstrapUi *ui,
                                    const clock_type::time_point &deadline) {
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

std::map<std::string, FileEntry> BuildManifestFileMap(const std::vector<FileEntry> &files) {
  std::map<std::string, FileEntry> out;
  for (const FileEntry &file : files)
    out[NormalizeRelativePath(Utf8Path(file.path))] = file;
  return out;
}

bool LiveFileMatchesEntry(const fs::path &install_root, const FileEntry &entry) {
  const fs::path live_path = install_root / Utf8Path(entry.path);
  if (!fs::is_regular_file(live_path))
    return false;

  std::error_code ec;
  const uint64_t live_size = fs::file_size(live_path, ec);
  if (ec)
    return false;
  if (entry.size != 0 && live_size != entry.size)
    return false;

  return true;
}

InstallSyncPlan BuildInstallSyncPlan(const fs::path &install_root, const InstallManifest &local_manifest,
                                     const UpdaterConfig &config, const RemotePayload &payload) {
  InstallSyncPlan plan;
  plan.version_change = CompareSemver(local_manifest.version, payload.version) < 0;
  plan.metadata_change = local_manifest.version != payload.version || local_manifest.launch_exe != payload.launch_exe ||
                         local_manifest.engine_library != payload.engine_library ||
                         local_manifest.local_manifest_name != payload.local_manifest_name;

  const std::vector<FileEntry> remote_files = ManifestFiles(payload.manifest_json);
  const auto remote_map = BuildManifestFileMap(remote_files);
  const auto local_map = BuildManifestFileMap(local_manifest.files);

  for (const FileEntry &remote_file : remote_files) {
    const std::string normalized = NormalizeRelativePath(Utf8Path(remote_file.path));
    const auto local_it = local_map.find(normalized);
    const bool manifest_matches = local_it != local_map.end() && local_it->second.size == remote_file.size &&
                                  ToLower(local_it->second.sha256) == ToLower(remote_file.sha256);
    if (manifest_matches && LiveFileMatchesEntry(install_root, remote_file)) {
      ++plan.unchanged_count;
      continue;
    }

    InstallAction action;
    action.kind = local_it == local_map.end() ? InstallActionKind::Add : InstallActionKind::Refresh;
    action.file = remote_file;
    plan.install_actions.push_back(std::move(action));
  }

  for (const auto &[normalized, local_file] : local_map) {
    if (remote_map.count(normalized) != 0)
      continue;
    if (IsPreserved(local_file.path, config))
      continue;
    const fs::path live_path = install_root / Utf8Path(local_file.path);
    if (fs::exists(live_path))
      plan.remove_paths.push_back(local_file.path);
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
  const std::string normalized = NormalizeRelativePath(Utf8Path(rel_path));
  for (const InstallAction &action : plan.install_actions) {
    if (NormalizeRelativePath(Utf8Path(action.file.path)) == normalized)
      return true;
  }
  for (const std::string &remove_path : plan.remove_paths) {
    if (NormalizeRelativePath(Utf8Path(remove_path)) == normalized)
      return true;
  }
  return false;
}

bool CanApplySyncInProcess(const BootstrapOptions &options, const InstallSyncPlan &plan) {
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
  return root;
}

void ValidateStagedPayload(const fs::path &stage_dir, const InstallSyncPlan &plan) {
  for (const InstallAction &action : plan.install_actions) {
    const FileEntry &file = action.file;
    const fs::path staged = stage_dir / Utf8Path(file.path);
    if (!fs::is_regular_file(staged))
      throw std::runtime_error("Missing staged file " + file.path);
    if (!file.sha256.empty() && ToLower(Sha256File(staged)) != ToLower(file.sha256))
      throw std::runtime_error("Hash mismatch for staged file " + file.path);
  }
}

void ApplyInstallSyncPlan(const fs::path &stage_dir, const fs::path &install_root, const InstallSyncPlan &plan,
                          const RemotePayload &payload, BootstrapUi *ui) {
  if (!plan.RequiresSync())
    return;

  const fs::path rollback_root = fs::temp_directory_path() / ("worr-update-rollback-" + RandomToken());
  fs::create_directories(rollback_root);

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
    const uint64_t total_steps = std::max<uint64_t>(1, plan.TotalApplySteps());
    uint64_t step = 0;
    for (const InstallAction &action : plan.install_actions) {
      const std::string &rel_path = action.file.path;
      const fs::path live_path = install_root / Utf8Path(rel_path);
      const fs::path staged_path = stage_dir / Utf8Path(rel_path);
      if (fs::exists(live_path))
        backup_file(rel_path);
      else
        created_paths.push_back(rel_path);
      fs::create_directories(live_path.parent_path());
      fs::copy_file(staged_path, live_path, fs::copy_options::overwrite_existing);

      ++step;
      if (ui) {
        ui->SetProgress("Synchronizing installation", step, total_steps);
        if (!ui->Pump())
          throw std::runtime_error("Install cancelled");
      }
    }

    for (const std::string &rel_path : plan.remove_paths) {
      const fs::path live_path = install_root / Utf8Path(rel_path);
      if (!fs::exists(live_path))
        continue;
      backup_file(rel_path);
      fs::remove(live_path);

      ++step;
      if (ui) {
        ui->SetProgress("Synchronizing installation", step, total_steps);
        if (!ui->Pump())
          throw std::runtime_error("Install cancelled");
      }
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
  if (ui && ui->Get() && !ui->Get()->WaitForMinimumDisplayTime())
    return 1;

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

  bool shared_window_handoff = false;
  auto *splash_ui = (ui && ui->Get()) ? dynamic_cast<SplashUi *>(ui->Get()) : nullptr;
  if (splash_ui && splash_ui->SupportsSharedWindowHandoff()) {
    if (void *native_handle = splash_ui->GetSharedWindowNativeHandle()) {
      if (splash_ui->PrepareSharedWindowHandoff()) {
        SetProcessEnvVar(kBootstrapWin32HwndEnv, PointerString(native_handle));
        shared_window_handoff = true;
        BootstrapTrace("LaunchEngineAndWait shared_window_handoff=1 hwnd=" + PointerString(native_handle));
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
  const bool bootstrap_transition = ui && dynamic_cast<SplashUi *>(ui->Get()) != nullptr;
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

std::string BuildSyncPromptDetail(const InstallSyncPlan &plan, const InstallManifest &local_manifest,
                                  const RemotePayload &payload, const std::string &warning) {
  std::ostringstream detail;
  if (plan.version_change) {
    detail << "Version " << payload.version << " is ready to synchronize";
    if (!local_manifest.version.empty())
      detail << " from " << local_manifest.version;
    detail << ".";
  } else {
    detail << "The local install differs from the authoritative manifest and can be synchronized.";
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
  BootstrapTrace("RunBootstrapFlow start role=" + std::string(RoleToCString(options.role)) +
                 " worker_mode=" + std::to_string(options.worker_mode ? 1 : 0) +
                 " approved_install=" + std::to_string(options.approved_install ? 1 : 0) +
                 " skip_update_check=" + std::to_string(options.skip_update_check ? 1 : 0) +
                 " quiet_status=" + std::to_string(options.quiet_status ? 1 : 0) +
                 " install_root=" + GenericPath(options.install_root));
  SessionShellWindowConfig session_shell_config;
  if (options.role == Role::Client)
    session_shell_config = LoadClientSessionShellWindowConfig(options.install_root, options.forwarded_args);
  BootstrapTrace("RunBootstrapFlow create_ui");
  UiHandle ui(options.role, options.quiet_status, options.role == Role::Client ? &session_shell_config : nullptr);
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
  std::optional<InstallSyncPlan> available_sync_plan;
  std::string warning;

  try {
    const auto deadline = clock_type::now() + std::chrono::milliseconds(kDiscoveryBudgetMs);
    RemotePayload discovered = DiscoverRemotePayload(config, options.role, &*ui, deadline);
    const int version_compare = CompareSemver(local_manifest.version, discovered.version);
    const InstallSyncPlan sync_plan = BuildInstallSyncPlan(options.install_root, local_manifest, config, discovered);
    BootstrapTrace("RunBootstrapFlow discovered version=" + discovered.version +
                   " version_compare=" + std::to_string(version_compare) +
                   " sync=" + std::to_string(sync_plan.RequiresSync() ? 1 : 0) + " plan=\"" +
                   DescribeInstallSyncPlan(sync_plan) + "\"");
    if (!discovered.version.empty() && (version_compare < 0 || (version_compare == 0 && sync_plan.RequiresSync()))) {
      available_update = discovered;
      available_sync_plan = sync_plan;
      const std::string state_result = sync_plan.version_change ? "update_available" : "sync_required";
      BootstrapTrace("RunBootstrapFlow update_available version=" + discovered.version + " state=" + state_result);
      SaveState(state_path, state_result, available_update);
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
    const int version_compare = CompareSemver(local_manifest.version, available_update->version);
    if (!available_sync_plan)
      available_sync_plan = BuildInstallSyncPlan(options.install_root, local_manifest, config, *available_update);
    if (version_compare > 0 || !available_sync_plan->RequiresSync()) {
      BootstrapTrace("RunBootstrapFlow stale_pending_sync ignored");
      available_update.reset();
      available_sync_plan.reset();
    }
  }

  if (available_update && available_sync_plan) {
    const std::string prompt_headline = available_sync_plan->version_change ? "Update required" : "Repair required";
    const std::string detail = BuildSyncPromptDetail(*available_sync_plan, local_manifest, *available_update, warning);
    const bool install = options.approved_install || ui->PromptInstall(prompt_headline, detail);
    if (!install) {
      BootstrapTrace("RunBootstrapFlow update_deferred");
      ui->SetStatus("Update deferred", "Launching the installed build without synchronizing it.");
    } else {
      const bool apply_in_process = CanApplySyncInProcess(options, *available_sync_plan);
      BootstrapTrace("RunBootstrapFlow apply_in_process=" + std::to_string(apply_in_process ? 1 : 0));
      if (!apply_in_process) {
        BootstrapTrace("RunBootstrapFlow launch_temp_worker");
        ui->SetStatus("Restarting updater", "Launching a temporary updater worker to synchronize the install.");
        return LaunchApprovedUpdateWorker(options);
      }

      const fs::path temp_root = fs::temp_directory_path() / ("worr-bootstrap-" + RandomToken());
      const fs::path archive_path = temp_root / available_update->update_package_name;
      const fs::path stage_dir = temp_root / "stage";
      fs::create_directories(stage_dir);

      if (available_sync_plan->RequiresPackagePayload()) {
        BootstrapTrace("RunBootstrapFlow worker_download_start");
        ui->SetStatus("Downloading update", "Fetching the package needed to synchronize the install.");
        const int package_timeout = 30 * 60 * 1000;
        HttpDownloadFile(available_update->package_url, archive_path, package_timeout, &*ui,
                         "Downloading update package");

        const Json::Value package = available_update->manifest_json["package"];
        const std::string package_hash = JsonString(package, "sha256");
        if (!package_hash.empty() && ToLower(Sha256File(archive_path)) != ToLower(package_hash))
          throw std::runtime_error("Downloaded package hash mismatch");

        BootstrapTrace("RunBootstrapFlow worker_extract_start root=" + GenericPath(temp_root));
        ExtractZip(archive_path, stage_dir, &*ui);
        ValidateStagedPayload(stage_dir, *available_sync_plan);
      } else {
        BootstrapTrace("RunBootstrapFlow worker_metadata_only_sync");
      }
#if defined(_WIN32)
      bool applied = false;
      for (int attempt = 0; attempt < kApplyRetryCount; ++attempt) {
        try {
          BootstrapTrace("RunBootstrapFlow apply_attempt=" + std::to_string(attempt + 1));
          ApplyInstallSyncPlan(stage_dir, options.install_root, *available_sync_plan, *available_update, &*ui);
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
                          "Finishing the previous process handoff before synchronizing the install.");
            SDL_Delay(kApplyRetryDelayMs);
            continue;
          }
          if (!IsProcessElevated()) {
            BootstrapTrace("RunBootstrapFlow elevation_requested");
            ui->SetStatus("Waiting for administrator approval", "WORR needs elevation to synchronize this install.");
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
      ApplyInstallSyncPlan(stage_dir, options.install_root, *available_sync_plan, *available_update, &*ui);
#endif
      BootstrapTrace("RunBootstrapFlow save_state_current");
      SaveState(state_path, "current", std::nullopt);
      std::error_code ignored;
      fs::remove_all(temp_root, ignored);

      const bool version_change = available_sync_plan->version_change;
      if (!options.worker_mode) {
        BootstrapTrace("RunBootstrapFlow in_process_sync_launch_engine");
        ui->SetStatus(version_change ? "Update installed" : "Repair complete",
                      version_change ? "Launching the updated hosted engine in the current bootstrap shell."
                                     : "Launching the synchronized hosted engine in the current bootstrap shell.");
        return LaunchEngineAndWait(options.install_root, options.launch_relpath, options.engine_library_relpath,
                                   options.forwarded_args, &ui);
      }

      if (!config.autolaunch) {
        BootstrapTrace("RunBootstrapFlow update_installed_no_autolaunch");
        ui->SetStatus(version_change ? "Update installed" : "Repair complete",
                      version_change ? "WORR was updated successfully. Launch it again to start the new build."
                                     : "The local install was synchronized successfully. Launch WORR again to start.");
        return 0;
      }

      BootstrapTrace("RunBootstrapFlow relaunch_updated_bootstrap");
      ui->SetStatus(version_change ? "Update installed" : "Repair complete",
                    version_change ? "Restarting the public WORR bootstrap with the updated build."
                                   : "Restarting the public WORR bootstrap with the synchronized install.");
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
  options.quiet_status = options.quiet_status || ForwardedArgsRequestHeadless(options.forwarded_args);
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
      if (arg == "--approved-install") {
        options.approved_install = true;
        continue;
      }
      if (arg == kQuietStatusArg) {
        options.quiet_status = true;
        continue;
      }
      options.forwarded_args.push_back(argv[i]);
    }
    options.quiet_status = options.quiet_status || ForwardedArgsRequestHeadless(options.forwarded_args);
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
