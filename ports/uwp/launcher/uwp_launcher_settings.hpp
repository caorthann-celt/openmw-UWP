#ifndef OPENMW_PORTS_UWP_LAUNCHER_UWP_LAUNCHER_SETTINGS_HPP
#define OPENMW_PORTS_UWP_LAUNCHER_UWP_LAUNCHER_SETTINGS_HPP

#include <filesystem>
#include <string>
#include <string_view>

#include <components/settings/parser.hpp>

namespace Uwp
{
    class ModConfig;

    inline constexpr int settingsTabCount = 8;

    class SettingsStore
    {
    public:
        explicit SettingsStore(const std::filesystem::path& localState);

        bool getBool(const char* category, const char* setting, bool fallback) const;
        int getInt(const char* category, const char* setting, int fallback) const;
        float getFloat(const char* category, const char* setting, float fallback) const;
        std::string getString(const char* category, const char* setting, std::string fallback) const;

        void set(const char* category, const char* setting, bool value);
        void set(const char* category, const char* setting, int value);
        void set(const char* category, const char* setting, float value);
        void set(const char* category, const char* setting, std::string_view value);
        void set(const char* category, const char* setting, const char* value);

        bool save();

    private:
        static std::string serializeFloat(float value);
        std::string getValue(const char* category, const char* setting, std::string fallback) const;

        std::filesystem::path mSettingsPath;
        Settings::CategorySettingValueMap mDefaults;
        Settings::CategorySettingValueMap mUser;
        bool mLoaded = false;
    };

    const char* settingsTabLabel(int tab);
    void drawSettingsPage(SettingsStore& settings, ModConfig& config, int tab, bool focus);
}

#endif
