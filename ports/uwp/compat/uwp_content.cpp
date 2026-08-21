#include "uwp_content.hpp"

#include <system_error>

#include <winrt/Windows.Storage.h>

namespace Files
{
    namespace
    {
        bool hasFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::is_regular_file(path, error);
        }
    }

    std::filesystem::path getUwpLocalStatePath()
    {
        return winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
    }

    std::filesystem::path getUwpExternalRootPath()
    {
        return LR"(E:\OpenMW)";
    }

    std::filesystem::path getUwpGameDataPath()
    {
        const std::filesystem::path external = getUwpExternalRootPath() / "data";
        if (hasFile(external / "Morrowind.esm"))
            return external;

        return getUwpLocalStatePath() / "data";
    }
}
