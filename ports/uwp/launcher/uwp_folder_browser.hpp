#ifndef OPENMW_PORTS_UWP_LAUNCHER_UWP_FOLDER_BROWSER_HPP
#define OPENMW_PORTS_UWP_LAUNCHER_UWP_FOLDER_BROWSER_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace Uwp
{
    struct BrowserEntry
    {
        std::wstring mName;
        std::filesystem::path mPath;
    };

    class FolderBrowser
    {
    public:
        explicit FolderBrowser(std::filesystem::path localState);

        void reset();
        bool open(int index);
        bool up(int& selection);

        const std::filesystem::path& currentPath() const;
        const std::vector<BrowserEntry>& entries() const;

    private:
        void refresh();
        bool withinRoot(const std::filesystem::path& path) const;

        std::filesystem::path mLocalState;
        std::filesystem::path mCurrentPath;
        std::filesystem::path mCurrentRoot;
        std::vector<BrowserEntry> mEntries;
    };
}

#endif
