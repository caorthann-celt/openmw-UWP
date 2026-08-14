#ifndef OPENMW_PORTS_UWP_COMPAT_UWP_PATH_HPP
#define OPENMW_PORTS_UWP_COMPAT_UWP_PATH_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace Files
{
    class UwpPath
    {
    public:
        explicit UwpPath(const std::string& applicationName);

        std::filesystem::path getUserConfigPath() const;
        std::filesystem::path getUserDataPath() const;
        std::filesystem::path getGlobalConfigPath() const;
        std::filesystem::path getLocalPath() const;
        std::filesystem::path getGlobalDataPath() const;
        std::filesystem::path getCachePath() const;
        std::vector<std::filesystem::path> getInstallPaths() const;

    private:
        std::filesystem::path mLocalState;
        std::filesystem::path mPackagePath;
        std::filesystem::path mGameDataPath;
    };
}

#endif
