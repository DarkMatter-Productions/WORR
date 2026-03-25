#pragma once

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string>

namespace worr::updater {

enum class Role {
  Client,
  Server,
};

const char *RoleToCString(Role role);

int RunLauncher(Role role, const std::string &launch_relative_path, const std::string &engine_library_relative_path,
                int argc, char **argv);
int RunWorker(int argc, char **argv);

#if defined(_WIN32)
int RunLauncherWide(Role role, const std::string &launch_relative_path,
                    const std::string &engine_library_relative_path, int argc, wchar_t **argv);
int RunWorkerWide(int argc, wchar_t **argv);
#endif

} // namespace worr::updater
