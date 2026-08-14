#ifndef OPENMW_PORTS_UWP_LAUNCHER_UWP_LAUNCHER_HPP
#define OPENMW_PORTS_UWP_LAUNCHER_UWP_LAUNCHER_HPP

#include <filesystem>

namespace Uwp
{
    enum class LauncherResult
    {
        launch,
        cancel,
        failed
    };

    LauncherResult runLauncher(const std::filesystem::path& localState);
}

#endif
