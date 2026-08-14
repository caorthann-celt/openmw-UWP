#include "uwp_folder_browser.hpp"

#include <algorithm>
#include <cwctype>
#include <system_error>

#include "../compat/uwp_content.hpp"

namespace
{
    std::wstring lowerCase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
        return value;
    }
}

namespace Uwp
{
    FolderBrowser::FolderBrowser(std::filesystem::path localState)
        : mLocalState(std::move(localState))
    {
        reset();
    }

    void FolderBrowser::reset()
    {
        mCurrentPath.clear();
        mCurrentRoot.clear();
        refresh();
    }

    bool FolderBrowser::open(int index)
    {
        if (index < 0 || index >= static_cast<int>(mEntries.size()))
            return false;
        const std::filesystem::path path = mEntries[index].mPath;
        std::error_code error;
        if (!std::filesystem::is_directory(path, error))
            return false;
        if (mCurrentPath.empty())
            mCurrentRoot = path;
        mCurrentPath = path;
        refresh();
        return true;
    }

    bool FolderBrowser::up(int& selection)
    {
        if (mCurrentPath.empty())
            return false;
        const std::filesystem::path previous = mCurrentPath;
        if (mCurrentPath == mCurrentRoot)
            reset();
        else
        {
            const std::filesystem::path parent = mCurrentPath.parent_path();
            if (!withinRoot(parent))
                return false;
            mCurrentPath = parent;
            refresh();
        }
        const auto found = std::find_if(
            mEntries.begin(), mEntries.end(), [&](const BrowserEntry& entry) { return entry.mPath == previous; });
        selection = found == mEntries.end() ? 0 : static_cast<int>(std::distance(mEntries.begin(), found));
        return true;
    }

    const std::filesystem::path& FolderBrowser::currentPath() const
    {
        return mCurrentPath;
    }

    const std::vector<BrowserEntry>& FolderBrowser::entries() const
    {
        return mEntries;
    }

    // offer storage roots first then browse their folders
    void FolderBrowser::refresh()
    {
        mEntries.clear();
        if (mCurrentPath.empty())
        {
            const std::filesystem::path external = Files::getUwpExternalRootPath();
            std::error_code error;
            if (std::filesystem::is_directory(external, error))
                mEntries.push_back({ L"External Storage", external });
            mEntries.push_back({ L"Internal Storage", mLocalState });
            return;
        }

        std::error_code error;
        std::filesystem::directory_iterator iterator(mCurrentPath, error);
        for (const auto& item : iterator)
            if (item.is_directory(error))
                mEntries.push_back({ item.path().filename().wstring(), item.path() });
        std::sort(mEntries.begin(), mEntries.end(), [](const BrowserEntry& left, const BrowserEntry& right) {
            return lowerCase(left.mName) < lowerCase(right.mName);
        });
    }

    // keep browsing inside the selected storage root
    bool FolderBrowser::withinRoot(const std::filesystem::path& path) const
    {
        const std::wstring root = lowerCase(mCurrentRoot.lexically_normal().wstring());
        const std::wstring candidate = lowerCase(path.lexically_normal().wstring());
        return candidate == root
            || (candidate.size() > root.size() && candidate.compare(0, root.size(), root) == 0
                && (candidate[root.size()] == L'\\' || candidate[root.size()] == L'/'));
    }
}
