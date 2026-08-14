#ifndef OPENMW_PORTS_UWP_COMPAT_UWP_CONTENT_HPP
#define OPENMW_PORTS_UWP_COMPAT_UWP_CONTENT_HPP

#include <filesystem>

namespace Files
{
    std::filesystem::path getUwpLocalStatePath();
    std::filesystem::path getUwpExternalRootPath();
    std::filesystem::path getUwpGameDataPath();
    void ensureUwpUserConfig();
}

#endif
