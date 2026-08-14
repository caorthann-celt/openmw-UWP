#include "uwp_mod_config.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <system_error>

#include <components/files/conversion.hpp>

#include "../compat/uwp_content.hpp"

namespace
{
    std::wstring lowerCase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
        return value;
    }

    std::wstring trim(std::wstring value)
    {
        const auto first = value.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
            return {};
        const auto last = value.find_last_not_of(L" \t\r\n");
        value = value.substr(first, last - first + 1);
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
            value = value.substr(1, value.size() - 2);
        return value;
    }

    std::wstring toWide(std::string_view value)
    {
        if (value.empty())
            return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        if (size > 0)
            MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    std::wstring keyFromLine(const std::string& line)
    {
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            return {};
        return lowerCase(trim(toWide(line.substr(0, separator))));
    }

    std::wstring valueFromLine(const std::string& line)
    {
        const auto separator = line.find('=');
        return separator == std::string::npos ? std::wstring() : trim(toWide(line.substr(separator + 1)));
    }

    bool isManagedKey(const std::wstring& key)
    {
        return key == L"data" || key == L"content" || key == L"fallback-archive";
    }

    bool hasExtension(const std::filesystem::path& path, std::initializer_list<const wchar_t*> extensions)
    {
        const std::wstring extension = lowerCase(path.extension().wstring());
        return std::any_of(
            extensions.begin(), extensions.end(), [&](const wchar_t* candidate) { return extension == candidate; });
    }

    bool samePath(const std::filesystem::path& left, const std::filesystem::path& right)
    {
        return lowerCase(left.lexically_normal().wstring()) == lowerCase(right.lexically_normal().wstring());
    }

    void sortDisabled(std::vector<Uwp::ModEntry>& entries, std::size_t first)
    {
        std::sort(entries.begin() + first, entries.end(), [](const Uwp::ModEntry& left, const Uwp::ModEntry& right) {
            return lowerCase(left.mName) < lowerCase(right.mName);
        });
    }
}

namespace Uwp
{
    ModConfig::ModConfig(std::filesystem::path localState)
        : mLocalState(std::move(localState))
        , mConfigPath(mLocalState / "openmw.cfg")
    {
    }

    // read the desktop openmw.cfg format
    bool ModConfig::load()
    {
        mOriginalLines.clear();
        mDataDirectories.clear();
        mContentFiles.clear();
        mArchiveFiles.clear();

        const std::filesystem::path base = Files::getUwpGameDataPath();
        std::error_code error;
        mDataDirectories.push_back({ L"BASE  " + base.filename().wstring(), {}, base,
            std::filesystem::is_directory(base, error), true, true });

        bool removedDuplicate = false;
        std::ifstream stream(mConfigPath);
        std::string line;
        while (std::getline(stream, line))
        {
            mOriginalLines.push_back(line);
            const std::wstring key = keyFromLine(line);
            const std::wstring value = valueFromLine(line);
            if (value.empty())
                continue;
            if (key == L"data")
            {
                const std::filesystem::path path = resolvePath(value);
                if (std::any_of(mDataDirectories.begin(), mDataDirectories.end(),
                        [&](const ModEntry& entry) { return samePath(entry.mPath, path); }))
                {
                    removedDuplicate = true;
                    continue;
                }
                mDataDirectories.push_back({ path.filename().wstring(), value, path,
                    std::filesystem::is_directory(path, error), true, false });
            }
            else if (key == L"content")
                mContentFiles.push_back({ value, value, {}, false, true, false });
            else if (key == L"fallback-archive")
                mArchiveFiles.push_back({ value, value, {}, false, true, false });
        }
        refresh();
        mDirty = removedDuplicate;
        return !stream.bad();
    }

    // replace only the lines managed by the launcher
    bool ModConfig::save()
    {
        if (!mDirty)
            return true;

        std::vector<std::string> managed;
        for (const ModEntry& entry : mDataDirectories)
            if (!entry.mPinned)
                managed.push_back(
                    "data=\"" + Files::pathToUnicodeString(std::filesystem::path(entry.mConfigValue)) + "\"");
        for (const ModEntry& entry : mArchiveFiles)
            if (entry.mEnabled)
                managed.push_back(
                    "fallback-archive=" + Files::pathToUnicodeString(std::filesystem::path(entry.mConfigValue)));
        for (const ModEntry& entry : mContentFiles)
            if (entry.mEnabled)
                managed.push_back("content=" + Files::pathToUnicodeString(std::filesystem::path(entry.mConfigValue)));

        std::vector<std::string> output;
        bool written = false;
        for (const std::string& line : mOriginalLines)
        {
            if (isManagedKey(keyFromLine(line)))
            {
                if (!written)
                {
                    output.insert(output.end(), managed.begin(), managed.end());
                    written = true;
                }
                continue;
            }
            output.push_back(line);
        }
        if (!written)
        {
            if (!output.empty() && !output.back().empty())
                output.emplace_back();
            output.insert(output.end(), managed.begin(), managed.end());
        }

        std::error_code error;
        std::filesystem::create_directories(mConfigPath.parent_path(), error);
        if (error)
            return false;
        const std::filesystem::path temporary = mConfigPath.wstring() + L".tmp";
        std::ofstream stream(temporary, std::ios::trunc);
        for (const std::string& line : output)
            stream << line << '\n';
        stream.close();
        if (!stream)
            return false;

        if (!ReplaceFileW(mConfigPath.c_str(), temporary.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                std::filesystem::remove(temporary, error);
                return false;
            }
            std::filesystem::rename(temporary, mConfigPath, error);
            if (error)
            {
                std::filesystem::remove(temporary, error);
                return false;
            }
        }

        mOriginalLines = std::move(output);
        mDirty = false;
        return true;
    }

    void ModConfig::refresh()
    {
        refreshFiles(mContentFiles, { L".esm", L".esp", L".omwgame", L".omwaddon", L".omwscripts" });
        refreshFiles(mArchiveFiles, { L".bsa" });
    }

    std::vector<ModEntry>& ModConfig::dataDirectories()
    {
        return mDataDirectories;
    }

    std::vector<ModEntry>& ModConfig::contentFiles()
    {
        return mContentFiles;
    }

    std::vector<ModEntry>& ModConfig::archiveFiles()
    {
        return mArchiveFiles;
    }

    bool ModConfig::addDataDirectory(const std::filesystem::path& path)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(path, error))
            return false;
        const std::wstring value = storePath(path);
        if (std::any_of(mDataDirectories.begin(), mDataDirectories.end(),
                [&](const ModEntry& entry) { return samePath(entry.mPath, path); }))
            return false;
        mDataDirectories.push_back({ path.filename().wstring(), value, path, true, true, false });
        mDirty = true;
        refresh();
        return true;
    }

    bool ModConfig::removeDataDirectory(int index)
    {
        if (index < 0 || index >= static_cast<int>(mDataDirectories.size()) || mDataDirectories[index].mPinned)
            return false;
        mDataDirectories.erase(mDataDirectories.begin() + index);
        mDirty = true;
        refresh();
        return true;
    }

    bool ModConfig::moveDataDirectory(int index, int direction)
    {
        const int destination = index + direction;
        if (index <= 0 || destination <= 0 || index >= static_cast<int>(mDataDirectories.size())
            || destination >= static_cast<int>(mDataDirectories.size()))
            return false;
        std::swap(mDataDirectories[index], mDataDirectories[destination]);
        mDirty = true;
        refresh();
        return true;
    }

    bool ModConfig::toggleEntry(std::vector<ModEntry>& entries, int index)
    {
        if (index < 0 || index >= static_cast<int>(entries.size()) || entries[index].mPinned)
            return false;
        entries[index].mEnabled = !entries[index].mEnabled;
        mDirty = true;
        return true;
    }

    bool ModConfig::moveEntry(std::vector<ModEntry>& entries, int index, int direction)
    {
        const int destination = index + direction;
        if (index < 0 || destination < 0 || index >= static_cast<int>(entries.size())
            || destination >= static_cast<int>(entries.size()))
            return false;
        std::swap(entries[index], entries[destination]);
        mDirty = true;
        return true;
    }

    std::filesystem::path ModConfig::resolvePath(const std::wstring& value) const
    {
        constexpr std::wstring_view userData = L"?userdata?";
        if (value.size() >= userData.size() && lowerCase(value.substr(0, userData.size())) == userData)
        {
            std::wstring relative = value.substr(userData.size());
            while (!relative.empty() && (relative.front() == L'\\' || relative.front() == L'/'))
                relative.erase(relative.begin());
            return mLocalState / relative;
        }
        return value;
    }

    std::wstring ModConfig::storePath(const std::filesystem::path& path) const
    {
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(path, mLocalState, error);
        if (!error && !relative.empty() && *relative.begin() != std::filesystem::path(L".."))
            return L"?userdata?\\" + relative.wstring();
        return path.wstring();
    }

    // find content and archives in the current data directories
    void ModConfig::refreshFiles(std::vector<ModEntry>& entries, std::initializer_list<const wchar_t*> extensions)
    {
        std::map<std::wstring, ModEntry> found;
        for (const ModEntry& directory : mDataDirectories)
        {
            std::error_code error;
            std::filesystem::directory_iterator iterator(directory.mPath, error);
            for (const auto& item : iterator)
            {
                if (!item.is_regular_file(error) || !hasExtension(item.path(), extensions))
                    continue;
                const std::wstring name = item.path().filename().wstring();
                found[lowerCase(name)] = { name, name, item.path(), true, false, false };
            }
        }

        for (ModEntry& entry : entries)
        {
            entry.mPresent = false;
            const auto match = found.find(lowerCase(entry.mConfigValue));
            if (match != found.end())
            {
                entry.mName = match->second.mName;
                entry.mPath = match->second.mPath;
                entry.mPresent = true;
                found.erase(match);
            }
        }
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                          [](const ModEntry& entry) { return !entry.mEnabled && !entry.mPresent; }),
            entries.end());
        const std::size_t firstDisabled = entries.size();
        for (auto& [key, entry] : found)
            entries.push_back(std::move(entry));
        sortDisabled(entries, firstDisabled);
    }
}
