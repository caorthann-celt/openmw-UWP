#include "uwp_path.hpp"

#include "uwp_content.hpp"

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>

namespace Files
{
    UwpPath::UwpPath(const std::string&)
        : mLocalState(getUwpLocalStatePath())
        , mPackagePath(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str())
        , mGameDataPath(getUwpGameDataPath())
    {
    }

    std::filesystem::path UwpPath::getUserConfigPath() const
    {
        return mLocalState;
    }

    std::filesystem::path UwpPath::getUserDataPath() const
    {
        return mLocalState;
    }

    std::filesystem::path UwpPath::getGlobalConfigPath() const
    {
        return {};
    }

    std::filesystem::path UwpPath::getLocalPath() const
    {
        return mPackagePath;
    }

    std::filesystem::path UwpPath::getGlobalDataPath() const
    {
        return mGameDataPath;
    }

    std::filesystem::path UwpPath::getCachePath() const
    {
        return mLocalState / "cache";
    }

    std::vector<std::filesystem::path> UwpPath::getInstallPaths() const
    {
        return {};
    }
}
