#include "bootstrap_shared.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

constexpr const char *kLaunchPath =
#if defined(_WIN32)
    "worr_ded_" CPUSTRING ".exe";
#else
    "worr_ded_" CPUSTRING;
#endif

constexpr const char *kEngineLibraryPath =
#if defined(_WIN32)
    "worr_ded_engine_" CPUSTRING ".dll";
#elif defined(__APPLE__)
    "worr_ded_engine_" CPUSTRING ".dylib";
#else
    "worr_ded_engine_" CPUSTRING ".so";
#endif

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t **argv) {
  return worr::updater::RunLauncherWide(worr::updater::Role::Server, kLaunchPath, kEngineLibraryPath, argc, argv);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return 1;
  const int code =
      worr::updater::RunLauncherWide(worr::updater::Role::Server, kLaunchPath, kEngineLibraryPath, argc, argv);
  LocalFree(argv);
  return code;
}
#else
int main(int argc, char **argv) {
  return worr::updater::RunLauncher(worr::updater::Role::Server, kLaunchPath, kEngineLibraryPath, argc, argv);
}
#endif
