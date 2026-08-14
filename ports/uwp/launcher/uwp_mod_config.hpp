#ifndef OPENMW_PORTS_UWP_LAUNCHER_UWP_MOD_CONFIG_HPP
#define OPENMW_PORTS_UWP_LAUNCHER_UWP_MOD_CONFIG_HPP

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace Uwp
{
    struct ModEntry
    {
        std::wstring mName;
        std::wstring mConfigValue;
        std::filesystem::path mPath;
        bool mPresent = true;
        bool mEnabled = true;
        bool mPinned = false;
    };

    class ModConfig
    {
    public:
        explicit ModConfig(std::filesystem::path localState);

        bool load();
        bool save();
        void refresh();

        std::vector<ModEntry>& dataDirectories();
        std::vector<ModEntry>& contentFiles();
        std::vector<ModEntry>& archiveFiles();

        bool addDataDirectory(const std::filesystem::path& path);
        bool removeDataDirectory(int index);
        bool moveDataDirectory(int index, int direction);
        bool toggleEntry(std::vector<ModEntry>& entries, int index);
        bool moveEntry(std::vector<ModEntry>& entries, int index, int direction);

    private:
        std::filesystem::path resolvePath(const std::wstring& value) const;
        std::wstring storePath(const std::filesystem::path& path) const;
        void refreshFiles(std::vector<ModEntry>& entries, std::initializer_list<const wchar_t*> extensions);

        std::filesystem::path mLocalState;
        std::filesystem::path mConfigPath;
        std::vector<std::string> mOriginalLines;
        std::vector<ModEntry> mDataDirectories;
        std::vector<ModEntry> mContentFiles;
        std::vector<ModEntry> mArchiveFiles;
        bool mDirty = false;
    };
}

#endif
