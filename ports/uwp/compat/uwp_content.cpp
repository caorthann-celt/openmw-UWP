#include "uwp_content.hpp"

#include <fstream>
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

    void ensureUwpUserConfig()
    {
        const std::filesystem::path config = getUwpLocalStatePath() / "openmw.cfg";
        if (hasFile(config))
            return;

        const std::filesystem::path data = getUwpGameDataPath();
        if (!hasFile(data / "Morrowind.esm"))
            return;

        std::ofstream stream(config);
        if (!stream)
            return;

        if (hasFile(data / "Morrowind.bsa"))
            stream << "fallback-archive=Morrowind.bsa\n";
        if (hasFile(data / "Tribunal.bsa"))
            stream << "fallback-archive=Tribunal.bsa\n";
        if (hasFile(data / "Bloodmoon.bsa"))
            stream << "fallback-archive=Bloodmoon.bsa\n";

        stream << "content=Morrowind.esm\n";
        if (hasFile(data / "Tribunal.esm"))
            stream << "content=Tribunal.esm\n";
        if (hasFile(data / "Bloodmoon.esm"))
            stream << "content=Bloodmoon.esm\n";
    }
}
