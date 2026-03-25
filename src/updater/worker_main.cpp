#include "bootstrap_shared.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#if defined(_WIN32)
int wmain(int argc, wchar_t **argv) { return worr::updater::RunWorkerWide(argc, argv); }

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return 1;
  const int code = worr::updater::RunWorkerWide(argc, argv);
  LocalFree(argv);
  return code;
}
#else
int main(int argc, char **argv) { return worr::updater::RunWorker(argc, argv); }
#endif
