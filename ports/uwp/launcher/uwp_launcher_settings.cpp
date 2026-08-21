#include "uwp_launcher_settings.hpp"

#include "uwp_mod_config.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <apps/openmw/mwsound/alext.h>
#include <components/files/conversion.hpp>

#ifndef ALC_ALL_DEVICES_SPECIFIER
#define ALC_ALL_DEVICES_SPECIFIER 0x1013
#endif

namespace
{
    constexpr float sCellSize = 8192.0f;

    // shared settings controls
    void focusFirstSetting(bool focus)
    {
        if (focus)
        {
            ImGui::FocusItem();
            ImGui::SetNavCursorVisible(true);
        }
    }

    bool drawSettingCheckbox(Uwp::SettingsStore& settings, const char* label, const char* category, const char* setting,
        bool fallback, bool enabled = true)
    {
        bool value = settings.getBool(category, setting, fallback);
        ImGui::BeginDisabled(!enabled);
        const bool edited = ImGui::Checkbox(label, &value);
        ImGui::EndDisabled();
        if (edited)
            settings.set(category, setting, value);
        return value;
    }

    int sliderStepDirection()
    {
        if (!ImGui::IsItemActive())
            return 0;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft, true)
            || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
            return -1;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true)
            || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
            return 1;
        return 0;
    }

    void drawSettingSliderInt(Uwp::SettingsStore& settings, const char* label, const char* category,
        const char* setting, int fallback, int minimum, int maximum, int step = 1, const char* format = "%d",
        bool enabled = true)
    {
        int value = settings.getInt(category, setting, fallback);
        const int previous = value;
        ImGui::BeginDisabled(!enabled);
        ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, true);
        ImGui::SliderInt(label, &value, minimum, maximum, format);
        ImGui::PopItemFlag();
        const int direction = sliderStepDirection();
        if (direction != 0)
            settings.set(category, setting, std::clamp(previous + direction * step, minimum, maximum));
        ImGui::EndDisabled();
    }

    void drawSettingSliderFloat(Uwp::SettingsStore& settings, const char* label, const char* category,
        const char* setting, float fallback, float minimum, float maximum, float step, const char* format = "%.2f",
        bool enabled = true)
    {
        float value = settings.getFloat(category, setting, fallback);
        const float previous = value;
        ImGui::BeginDisabled(!enabled);
        ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, true);
        ImGui::SliderFloat(label, &value, minimum, maximum, format);
        ImGui::PopItemFlag();
        const int direction = sliderStepDirection();
        if (direction != 0)
            settings.set(category, setting, std::clamp(previous + direction * step, minimum, maximum));
        ImGui::EndDisabled();
    }

    int drawSettingCombo(Uwp::SettingsStore& settings, const char* label, const char* category, const char* setting,
        int fallback, const char* const items[], int count, const int* values = nullptr, bool enabled = true)
    {
        const int value = settings.getInt(category, setting, fallback);
        int selected = 0;
        for (int index = 0; index < count; ++index)
        {
            if ((values ? values[index] : index) == value)
                selected = index;
        }
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Combo(label, &selected, items, count))
            settings.set(category, setting, values ? values[selected] : selected);
        ImGui::EndDisabled();
        return values ? values[selected] : selected;
    }

    void drawStringCombo(Uwp::SettingsStore& settings, const char* label, const char* category, const char* setting,
        const std::vector<std::string>& values)
    {
        std::string current = settings.getString(category, setting, {});
        if (!current.empty() && std::find(values.begin(), values.end(), current) == values.end())
        {
            current.clear();
            settings.set(category, setting, "");
        }
        const char* preview = current.empty() ? "Default" : current.c_str();
        if (!ImGui::BeginCombo(label, preview))
            return;

        if (ImGui::Selectable("Default", current.empty()))
            settings.set(category, setting, "");
        for (const std::string& value : values)
        {
            if (ImGui::Selectable(value.c_str(), current == value))
                settings.set(category, setting, value);
        }
        ImGui::EndCombo();
    }

    // audio choices
    std::vector<std::string> enumerateOpenALDevices()
    {
        std::vector<std::string> devices;
        const ALCchar* names = alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")
            ? alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER)
            : alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
        while (names && *names)
        {
            devices.emplace_back(names);
            names += std::strlen(names) + 1;
        }
        return devices;
    }

    std::vector<std::string> enumerateHrtfProfiles()
    {
        std::vector<std::string> profiles;
        ALCdevice* device = alcOpenDevice(nullptr);
        if (!device)
            return profiles;

        if (alcIsExtensionPresent(device, "ALC_SOFT_HRTF"))
        {
            LPALCGETSTRINGISOFT getString = nullptr;
            void* function = alcGetProcAddress(device, "alcGetStringiSOFT");
            std::memcpy(&getString, &function, sizeof(function));
            ALCint count = 0;
            alcGetIntegerv(device, ALC_NUM_HRTF_SPECIFIERS_SOFT, 1, &count);
            profiles.reserve(count);
            for (ALCint index = 0; getString && index < count; ++index)
            {
                const ALCchar* profile = getString(device, ALC_HRTF_SPECIFIER_SOFT, index);
                if (!profile || !*profile)
                    break;
                profiles.emplace_back(profile);
            }
        }
        alcCloseDevice(device);
        return profiles;
    }

    // language choices
    std::vector<std::string> splitLocales(const std::string& value)
    {
        std::vector<std::string> locales;
        std::istringstream stream(value);
        std::string locale;
        while (std::getline(stream, locale, ','))
        {
            const size_t begin = locale.find_first_not_of(" \t");
            const size_t end = locale.find_last_not_of(" \t");
            if (begin != std::string::npos)
                locales.push_back(locale.substr(begin, end - begin + 1));
        }
        return locales;
    }

    void saveLocales(Uwp::SettingsStore& settings, const std::vector<std::string>& locales)
    {
        std::string value;
        for (const std::string& locale : locales)
        {
            if (!value.empty())
                value += ',';
            value += locale;
        }
        settings.set("General", "preferred locales", value);
    }

    const char* languageName(const std::string& code)
    {
        if (code == "en")
            return "English";
        if (code == "fr")
            return "French";
        if (code == "de")
            return "German";
        if (code == "pl")
            return "Polish";
        if (code == "ru")
            return "Russian";
        if (code == "sv")
            return "Swedish";
        return code.c_str();
    }

    std::vector<std::string> findLanguages(Uwp::ModConfig& config)
    {
        std::vector<std::string> languages = { "en", "fr", "de", "pl", "ru", "sv" };
        std::set<std::string> seen(languages.begin(), languages.end());
        for (const Uwp::ModEntry& entry : config.dataDirectories())
        {
            const std::filesystem::path root = entry.mPath / "l10n";
            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(
                root, std::filesystem::directory_options::skip_permission_denied, error);
            const std::filesystem::recursive_directory_iterator end;
            while (!error && iterator != end)
            {
                if (iterator->is_regular_file(error) && iterator->path().extension() == ".yaml")
                {
                    const std::string language = Files::pathToUnicodeString(iterator->path().stem());
                    if (language != "gmst" && seen.insert(language).second)
                        languages.push_back(language);
                }
                iterator.increment(error);
            }
        }
        return languages;
    }

    // settings pages
    void drawDisplaySettings(Uwp::SettingsStore& settings, bool focus)
    {
        static constexpr const char* resolutions[] = { "1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160" };
        static constexpr int widths[] = { 1280, 1920, 2560, 3840 };
        static constexpr int heights[] = { 720, 1080, 1440, 2160 };
        const int width = settings.getInt("Video", "resolution x", 1920);
        const int height = settings.getInt("Video", "resolution y", 1080);
        int resolution = -1;
        for (int index = 0; index < 4; ++index)
        {
            if (widths[index] == width && heights[index] == height)
                resolution = index;
        }
        const std::string currentResolution
            = resolution >= 0 ? resolutions[resolution] : std::to_string(width) + " x " + std::to_string(height);
        if (ImGui::BeginCombo("Resolution", currentResolution.c_str()))
        {
            for (int index = 0; index < 4; ++index)
            {
                if (ImGui::Selectable(resolutions[index], resolution == index))
                {
                    settings.set("Video", "resolution x", widths[index]);
                    settings.set("Video", "resolution y", heights[index]);
                }
            }
            ImGui::EndCombo();
        }
        focusFirstSetting(focus);

        static constexpr const char* windowModes[] = { "Fullscreen", "Windowed Fullscreen", "Windowed" };
        drawSettingCombo(settings, "Window mode", "Video", "window mode", 0, windowModes, 3);
        static constexpr const char* vsync[] = { "Off", "On", "Adaptive" };
        drawSettingCombo(settings, "VSync", "Video", "vsync mode", 1, vsync, 3);

        static constexpr const char* frameLimits[] = { "30 FPS", "60 FPS", "120 FPS", "Unlimited" };
        static constexpr float frameLimitValues[] = { 30.0f, 60.0f, 120.0f, 0.0f };
        const float frameLimit = settings.getFloat("Video", "framerate limit", 300.0f);
        int selectedFrameLimit = frameLimit <= 0.0f || frameLimit > 120.0f ? 3 : 1;
        if (frameLimit > 120.0f)
            settings.set("Video", "framerate limit", 0.0f);
        for (int index = 0; index < 3; ++index)
        {
            if (frameLimit == frameLimitValues[index])
                selectedFrameLimit = index;
        }
        if (ImGui::Combo("Frame limit", &selectedFrameLimit, frameLimits, 4))
            settings.set("Video", "framerate limit", frameLimitValues[selectedFrameLimit]);

        drawSettingCheckbox(settings, "Show FPS", "UWP", "show fps", false);
        drawSettingSliderFloat(
            settings, "Field of view", "Camera", "field of view", 60.0f, 30.0f, 110.0f, 1.0f, "%.0f");
        drawSettingSliderFloat(settings, "Gamma", "Video", "gamma", 1.0f, 0.1f, 3.0f, 0.01f);
    }

    void drawGameplaySettings(Uwp::SettingsStore& settings, bool focus)
    {
        drawSettingSliderInt(settings, "Difficulty", "Game", "difficulty", 0, -100, 100);
        focusFirstSetting(focus);
        drawSettingSliderInt(
            settings, "Actor processing range", "Game", "actors processing range", 7168, 3584, 7168, 128);
        drawSettingCheckbox(settings, "Autosave when resting", "Saves", "autosave", true);
        drawSettingCheckbox(settings, "Always use best attack", "Game", "best attack", false);
        drawSettingCheckbox(settings, "Trainers choose offered skills by base value", "Game",
            "trainers training skills based on base skill", false);
        drawSettingCheckbox(settings, "Steal from knocked out actors in combat", "Game",
            "always allow stealing from knocked out actors", false);
        drawSettingCheckbox(settings, "Always allow actors to follow over water", "Game",
            "allow actors to follow over water surface", true);
        drawSettingCheckbox(
            settings, "Permanent barter disposition changes", "Game", "barter disposition change is permanent", false);
        drawSettingCheckbox(settings, "Use navigation mesh for pathfinding", "Navigator", "enable", true);
        drawSettingCheckbox(settings, "Followers defend immediately", "Game", "followers attack on sight", false);
        drawSettingCheckbox(settings, "Uncapped damage fatigue", "Game", "uncapped damage fatigue", false);
        drawSettingCheckbox(settings, "Soulgem values rebalance", "Game", "rebalance soul gem values", false);
        drawSettingCheckbox(settings, "Merchant equipping fix", "Game", "prevent merchant equipping", false);
        drawSettingCheckbox(settings, "Day night switch nodes", "Game", "day night switches", true);
        drawSettingCheckbox(settings, "Classic reflected absorb spells behavior", "Game",
            "classic reflected absorb spells behavior", true);
        drawSettingCheckbox(settings, "Only magical ammo bypass resistance", "Game",
            "only appropriate ammunition bypasses resistance", false);
        drawSettingCheckbox(
            settings, "Can loot during death animation", "Game", "can loot during death animation", true);
        drawSettingCheckbox(settings, "Enchanted weapons are magical", "Game", "enchanted weapons are magical", true);
        drawSettingCheckbox(settings, "Classic calm spells behavior", "Game", "classic calm spells behavior", true);
        drawSettingCheckbox(settings, "Racial variation in speed fix", "Game", "normalise race speed", false);
        drawSettingCheckbox(settings, "Swim upward correction", "Game", "swim upward correction", false);
        drawSettingCheckbox(settings, "NPCs avoid collisions", "Game", "NPCs avoid collisions", false);
        drawSettingCheckbox(settings, "Graphic herbalism", "Game", "graphic herbalism", true);

        static constexpr const char* handToHand[] = { "Off", "Affect Werewolves", "Do Not Affect Werewolves" };
        drawSettingCombo(settings, "Factor strength into hand-to-hand combat", "Game",
            "strength influences hand to hand", 0, handToHand, 3);
        drawSettingSliderInt(settings, "Background physics threads", "Physics", "async num threads", 1, 0, 99);
        static constexpr const char* collisionShapes[] = { "Axis-Aligned Bounding Box", "Rotating Box", "Cylinder" };
        drawSettingCombo(
            settings, "Actor collision shape type", "Game", "actor collision shape type", 0, collisionShapes, 3);
    }

    void drawVisualSettings(Uwp::SettingsStore& settings, bool focus)
    {
        ImGui::SeparatorText("Animations");
        drawSettingCheckbox(
            settings, "Player movement ignores animation", "Game", "player movement ignores animation", false);
        focusFirstSetting(focus);
        drawSettingCheckbox(settings, "Use magic item animation", "Game", "use magic item animations", false);
        const bool animSources = drawSettingCheckbox(
            settings, "Use additional animation sources", "Game", "use additional anim sources", false);
        if (!animSources)
        {
            settings.set("Game", "weapon sheathing", false);
            settings.set("Game", "shield sheathing", false);
        }
        drawSettingCheckbox(settings, "Weapon sheathing", "Game", "weapon sheathing", false, animSources);
        drawSettingCheckbox(settings, "Smooth movement", "Game", "smooth movement", false);
        drawSettingCheckbox(settings, "Turn to movement direction", "Game", "turn to movement direction", false);
        drawSettingCheckbox(settings, "Smooth animation transitions", "Game", "smooth animation transitions", false);
        drawSettingCheckbox(settings, "Shield sheathing", "Game", "shield sheathing", false, animSources);

        ImGui::SeparatorText("Textures");
        static constexpr const char* textureFiltering[] = { "Bilinear", "Trilinear" };
        const std::string mipmap = settings.getString("General", "texture mipmap", "nearest");
        int textureFilter = mipmap == "linear" ? 1 : 0;
        if (ImGui::Combo("Texture filtering", &textureFilter, textureFiltering, 2))
        {
            settings.set("General", "texture mag filter", "linear");
            settings.set("General", "texture min filter", "linear");
            settings.set("General", "texture mipmap", textureFilter == 0 ? "nearest" : "linear");
        }
        static constexpr const char* anisotropy[] = { "Off", "2x", "4x", "8x", "16x" };
        static constexpr int anisotropyValues[] = { 0, 2, 4, 8, 16 };
        drawSettingCombo(settings, "Anisotropy", "General", "anisotropy", 4, anisotropy, 5, anisotropyValues);

        ImGui::SeparatorText("Shaders");
        drawSettingCheckbox(
            settings, "Bump/reflect map local lighting", "Shaders", "apply lighting to environment maps", false);
        drawSettingCheckbox(settings, "Auto use object normal maps", "Shaders", "auto use object normal maps", false);
        drawSettingCheckbox(
            settings, "Auto use object specular maps", "Shaders", "auto use object specular maps", false);
        drawSettingCheckbox(settings, "Weather particle occlusion", "Shaders", "weather particle occlusion", false);
        drawSettingCheckbox(settings, "Soft particles", "Shaders", "soft particles", false);
        drawSettingCheckbox(
            settings, "Auto use terrain specular maps", "Shaders", "auto use terrain specular maps", false);
        drawSettingCheckbox(settings, "Auto use terrain normal maps", "Shaders", "auto use terrain normal maps", false);
        drawSettingCheckbox(
            settings, "Adjust coverage for alpha test", "Shaders", "adjust coverage for alpha test", true);

        ImGui::SeparatorText("Fog");
        drawSettingCheckbox(settings, "Radial fog", "Fog", "radial fog", false);
        drawSettingCheckbox(settings, "Exponential fog", "Fog", "exponential fog", false);
        const bool skyBlending = drawSettingCheckbox(settings, "Sky blending", "Fog", "sky blending", false);
        drawSettingSliderFloat(
            settings, "Sky blending start", "Fog", "sky blending start", 0.8f, 0.0f, 1.0f, 0.005f, "%.3f", skyBlending);

        ImGui::SeparatorText("Terrain");
        float viewingDistance = settings.getFloat("Camera", "viewing distance", 7168.0f) / sCellSize;
        const float previousViewingDistance = viewingDistance;
        ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, true);
        ImGui::SliderFloat("Viewing distance", &viewingDistance, 0.25f, 99.99f, "%.3f cells");
        ImGui::PopItemFlag();
        const int viewingDistanceDirection = sliderStepDirection();
        if (viewingDistanceDirection != 0)
        {
            viewingDistance = std::clamp(previousViewingDistance + viewingDistanceDirection * 0.125f, 0.25f, 99.99f);
            settings.set(
                "Camera", "viewing distance", static_cast<float>(static_cast<int>(viewingDistance * sCellSize)));
        }
        const bool distantLand = settings.getBool("Terrain", "distant terrain", false)
            && settings.getBool("Terrain", "object paging", true);
        bool distantLandValue = distantLand;
        if (ImGui::Checkbox("Distant land", &distantLandValue))
        {
            settings.set("Terrain", "distant terrain", distantLandValue);
            settings.set("Terrain", "object paging", distantLandValue);
        }
        drawSettingCheckbox(
            settings, "Active grid object paging", "Terrain", "object paging active grid", true, distantLandValue);
        drawSettingSliderFloat(settings, "Object paging min size", "Terrain", "object paging min size", 0.01f, 0.0f,
            0.25f, 0.005f, "%.3f", distantLandValue);

        ImGui::SeparatorText("Post Processing");
        const bool postProcessing
            = drawSettingCheckbox(settings, "Enable post processing", "Post Processing", "enabled", false);
        drawSettingCheckbox(
            settings, "Transparent postpass", "Post Processing", "transparent postpass", true, postProcessing);
        drawSettingSliderFloat(settings, "Auto exposure speed", "Post Processing", "auto exposure speed", 0.9f, 0.01f,
            10.0f, 0.001f, "%.3f", postProcessing);

        ImGui::SeparatorText("Shadows");
        const bool shadowsEnabled = settings.getBool("Shadows", "enable shadows", false);
        bool actorShadows = shadowsEnabled && settings.getBool("Shadows", "actor shadows", false);
        bool playerShadows = shadowsEnabled && settings.getBool("Shadows", "player shadows", false);
        bool objectShadows = shadowsEnabled && settings.getBool("Shadows", "object shadows", false);
        bool terrainShadows = shadowsEnabled && settings.getBool("Shadows", "terrain shadows", false);
        bool shadowsChanged = ImGui::Checkbox("Enable player shadows", &playerShadows);
        shadowsChanged = ImGui::Checkbox("Enable actor shadows", &actorShadows) || shadowsChanged;
        shadowsChanged = ImGui::Checkbox("Enable object shadows", &objectShadows) || shadowsChanged;
        drawSettingCheckbox(settings, "Enable indoor shadows", "Shadows", "enable indoor shadows", true);
        shadowsChanged = ImGui::Checkbox("Enable terrain shadows", &terrainShadows) || shadowsChanged;
        if (shadowsChanged)
        {
            settings.set("Shadows", "enable shadows", actorShadows || playerShadows || objectShadows || terrainShadows);
            settings.set("Shadows", "actor shadows", actorShadows);
            settings.set("Shadows", "player shadows", playerShadows);
            settings.set("Shadows", "object shadows", objectShadows);
            settings.set("Shadows", "terrain shadows", terrainShadows);
        }
        static constexpr const char* sceneBounds[] = { "Bounds", "Primitives", "None" };
        const std::string sceneBoundsValue = settings.getString("Shadows", "compute scene bounds", "bounds");
        int sceneBoundsIndex = sceneBoundsValue == "primitives" ? 1 : sceneBoundsValue == "none" ? 2 : 0;
        if (ImGui::Combo("Shadow planes computation method", &sceneBoundsIndex, sceneBounds, 3))
        {
            static constexpr const char* values[] = { "bounds", "primitives", "none" };
            settings.set("Shadows", "compute scene bounds", values[sceneBoundsIndex]);
        }
        static constexpr const char* shadowResolution[] = { "512", "1024", "2048", "4096" };
        static constexpr int shadowResolutionValues[] = { 512, 1024, 2048, 4096 };
        drawSettingCombo(settings, "Shadow map resolution", "Shadows", "shadow map resolution", 1024, shadowResolution,
            4, shadowResolutionValues);
        const int shadowDistance = settings.getInt("Shadows", "maximum shadow map distance", 8192);
        bool limitShadowDistance = shadowDistance > 0;
        if (ImGui::Checkbox("Limit shadow distance", &limitShadowDistance))
            settings.set("Shadows", "maximum shadow map distance", limitShadowDistance ? 512 : 0);
        drawSettingSliderInt(settings, "Shadow distance limit", "Shadows", "maximum shadow map distance", 8192, 512,
            81920, 128, "%d units", limitShadowDistance);
        drawSettingSliderFloat(settings, "Fade start multiplier", "Shadows", "shadow fade start", 0.9f, 0.0f, 1.0f,
            0.01f, "%.2f", limitShadowDistance);

        ImGui::SeparatorText("Lighting");
        drawSettingCheckbox(settings, "Force per-pixel lighting", "Shaders", "force per pixel lighting", false);
        drawSettingCheckbox(settings, "Particle point lighting", "Shaders", "particle point lighting", true);
        drawSettingCheckbox(settings, "Clamp lighting", "Shaders", "clamp lighting", true);
        drawSettingCheckbox(settings, "Match sunlight to sun", "Shaders", "match sunlight to sun", false);
        drawSettingCheckbox(settings, "Classic light falloff", "Shaders", "classic falloff", false);
        static constexpr const char* maxLights[] = { "8", "16", "24", "32", "40", "48", "56", "64" };
        static constexpr int maxLightValues[] = { 8, 16, 24, 32, 40, 48, 56, 64 };
        drawSettingCombo(settings, "Maximum lights", "Shaders", "max lights", 16, maxLights, 8, maxLightValues);
        drawSettingSliderFloat(settings, "Maximum light distance", "Shaders", "maximum light distance", 8192.0f, 0.0f,
            8192.0f, 128.0f, "%.0f");
        drawSettingSliderFloat(
            settings, "Light radius multiplier", "Shaders", "light radius multiplier", 1.75f, 1.0f, 5.0f, 0.01f);
        drawSettingSliderFloat(settings, "Minimum interior brightness", "Shaders", "minimum interior brightness", 0.08f,
            0.0f, 1.0f, 0.01f);

        ImGui::SeparatorText("Water");
        drawSettingCheckbox(settings, "Water shaders", "Water", "shader", false);
        drawSettingCheckbox(settings, "Refraction", "Water", "refraction", false);
        drawSettingCheckbox(settings, "Sunlight scattering", "Water", "sunlight scattering", true);
        drawSettingCheckbox(settings, "Wobbly shores", "Water", "wobbly shores", true);
        static constexpr const char* waterTexture[] = { "Low (512)", "Medium (1024)", "High (2048)" };
        static constexpr int waterTextureValues[] = { 512, 1024, 2048 };
        drawSettingCombo(settings, "Water texture", "Water", "rtt size", 512, waterTexture, 3, waterTextureValues);
        static constexpr const char* reflectionDetail[]
            = { "Sky", "Terrain", "World", "Objects", "Actors", "Groundcover" };
        drawSettingCombo(settings, "Reflection detail", "Water", "reflection detail", 2, reflectionDetail, 6);
        static constexpr const char* rainRipples[] = { "Simple", "Sparse", "Dense" };
        drawSettingCombo(settings, "Rain ripple detail", "Water", "rain ripple detail", 1, rainRipples, 3);
    }

    void drawAudioSettings(Uwp::SettingsStore& settings, bool focus)
    {
        static std::vector<std::string> devices;
        static std::vector<std::string> profiles;
        static bool enumerated = false;
        if (!enumerated)
        {
            devices = enumerateOpenALDevices();
            profiles = enumerateHrtfProfiles();
            enumerated = true;
        }

        drawStringCombo(settings, "Audio device", "Sound", "device", devices);
        focusFirstSetting(focus);
        static constexpr const char* hrtf[] = { "Automatic", "Off", "On" };
        static constexpr int hrtfValues[] = { -1, 0, 1 };
        drawSettingCombo(settings, "HRTF", "Sound", "hrtf enable", -1, hrtf, 3, hrtfValues);
        drawStringCombo(settings, "HRTF profile", "Sound", "hrtf", profiles);
        drawSettingSliderFloat(settings, "Doppler factor", "Sound", "doppler factor", 0.25f, 0.0f, 1.0f, 0.01f);
        drawSettingCheckbox(settings, "Use camera as sound listener", "Sound", "camera listener", false);

        ImGui::SeparatorText("Volume");
        drawSettingSliderFloat(settings, "Master volume", "Sound", "master volume", 1.0f, 0.0f, 1.0f, 0.01f);
        drawSettingSliderFloat(settings, "Music volume", "Sound", "music volume", 0.5f, 0.0f, 1.0f, 0.01f);
        drawSettingSliderFloat(settings, "Effects volume", "Sound", "sfx volume", 1.0f, 0.0f, 1.0f, 0.01f);
        drawSettingSliderFloat(settings, "Voice volume", "Sound", "voice volume", 0.8f, 0.0f, 1.0f, 0.01f);
        drawSettingSliderFloat(settings, "Footsteps volume", "Sound", "footsteps volume", 0.2f, 0.0f, 1.0f, 0.01f);
    }

    void drawInterfaceSettings(Uwp::SettingsStore& settings, bool focus)
    {
        static constexpr const char* showOwned[] = { "Off", "Tooltip", "Crosshair", "Tooltip and Crosshair" };
        drawSettingCombo(settings, "Show owned objects", "Game", "show owned", 0, showOwned, 4);
        focusFirstSetting(focus);
        drawSettingSliderFloat(settings, "GUI scaling factor", "GUI", "scaling factor", 1.0f, 0.5f, 8.0f, 0.25f);
        drawSettingSliderInt(settings, "Font size", "GUI", "font size", 16, 12, 18);
        drawSettingCheckbox(settings, "Show effect duration", "Game", "show effect duration", false);
        drawSettingCheckbox(settings, "Show melee info", "Game", "show melee info", false);
        drawSettingCheckbox(settings, "Change dialogue topic color", "GUI", "color topic enable", false);
        drawSettingCheckbox(settings, "Can zoom on maps", "Map", "allow zooming", false);
        drawSettingCheckbox(settings, "Show enchant chance", "Game", "show enchant chance", false);
        drawSettingCheckbox(settings, "Show projectile damage", "Game", "show projectile damage", false);
        drawSettingCheckbox(settings, "Stretch menu background", "GUI", "stretch menu background", false);
        const bool controllerMenus
            = drawSettingCheckbox(settings, "Enable controller menus", "GUI", "controller menus", true);
        drawSettingCheckbox(
            settings, "Show controller tooltips by default", "GUI", "controller tooltips", true, controllerMenus);

        ImGui::SeparatorText("Other interface options");
        drawSettingSliderFloat(settings, "Menu transparency", "GUI", "menu transparency", 0.84f, 0.0f, 1.0f, 0.01f);
        drawSettingSliderFloat(settings, "Tooltip delay", "GUI", "tooltip delay", 0.0f, 0.0f, 1.0f, 0.01f);
        drawSettingCheckbox(settings, "Subtitles", "GUI", "subtitles", false);
        drawSettingCheckbox(settings, "Crosshair", "HUD", "crosshair", true);
    }

    void drawMiscSettings(Uwp::SettingsStore& settings, bool focus)
    {
        ImGui::SeparatorText("Saves");
        drawSettingSliderInt(settings, "Maximum quicksaves", "Saves", "max quicksaves", 1, 1, 99);
        focusFirstSetting(focus);

        ImGui::SeparatorText("Screenshots");
        static constexpr const char* screenshotFormats[] = { "JPG", "PNG", "TGA" };
        const std::string screenshotFormat = settings.getString("General", "screenshot format", "png");
        int screenshotFormatIndex = screenshotFormat == "jpg" ? 0 : screenshotFormat == "tga" ? 2 : 1;
        if (ImGui::Combo("Screenshot format", &screenshotFormatIndex, screenshotFormats, 3))
        {
            static constexpr const char* values[] = { "jpg", "png", "tga" };
            settings.set("General", "screenshot format", values[screenshotFormatIndex]);
        }
        drawSettingCheckbox(settings, "Notify on saved screenshot", "General", "notify on saved screenshot", false);
    }

    void drawInputSettings(Uwp::SettingsStore& settings, bool focus)
    {
        drawSettingCheckbox(settings, "Controller enabled", "Input", "enable controller", true);
        focusFirstSetting(focus);
        drawSettingSliderFloat(settings, "Look sensitivity", "Input", "camera sensitivity", 1.0f, 0.2f, 5.0f, 0.05f);
        drawSettingCheckbox(settings, "Invert horizontal look", "Input", "invert x axis", false);
        drawSettingCheckbox(settings, "Invert vertical look", "Input", "invert y axis", false);
        drawSettingSliderFloat(
            settings, "Menu cursor speed", "Input", "gamepad cursor speed", 1.0f, 0.25f, 3.0f, 0.05f);
        drawSettingSliderFloat(settings, "Stick dead zone", "Input", "joystick dead zone", 0.1f, 0.0f, 0.5f, 0.01f);
    }

    void drawLanguageSettings(Uwp::SettingsStore& settings, Uwp::ModConfig& config, bool focus)
    {
        std::vector<std::string> locales = splitLocales(settings.getString("General", "preferred locales", "en"));
        if (locales.empty())
            locales.push_back("en");
        static std::vector<std::string> languages;
        if (focus || languages.empty())
            languages = findLanguages(config);
        for (const std::string& locale : locales)
        {
            if (std::find(languages.begin(), languages.end(), locale) == languages.end())
                languages.push_back(locale);
        }

        if (ImGui::BeginCombo("Primary language", languageName(locales[0])))
        {
            for (const std::string& language : languages)
            {
                if (ImGui::Selectable(languageName(language), locales[0] == language))
                {
                    locales[0] = language;
                    saveLocales(settings, locales);
                }
            }
            ImGui::EndCombo();
        }
        focusFirstSetting(focus);

        const char* secondaryPreview = locales.size() > 1 ? languageName(locales[1]) : "None";
        if (ImGui::BeginCombo("Secondary language", secondaryPreview))
        {
            if (ImGui::Selectable("None", locales.size() == 1))
            {
                locales.resize(1);
                saveLocales(settings, locales);
            }
            for (const std::string& language : languages)
            {
                const bool selected = locales.size() > 1 && locales[1] == language;
                if (ImGui::Selectable(languageName(language), selected))
                {
                    if (locales.size() > 1)
                        locales[1] = language;
                    else
                        locales.push_back(language);
                    saveLocales(settings, locales);
                }
            }
            ImGui::EndCombo();
        }
        drawSettingCheckbox(settings, "Strings from ESM files have priority", "General", "gmst overrides l10n", true);
    }
}

// settings file
Uwp::SettingsStore::SettingsStore(const std::filesystem::path& localState)
    : mSettingsPath(localState / "settings.cfg")
{
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        return;

    try
    {
        Settings::SettingsFileParser parser;
        parser.loadSettingsFile(
            std::filesystem::path(modulePath).parent_path() / "defaults.bin", mDefaults, true, false);
        if (std::filesystem::exists(mSettingsPath))
            parser.loadSettingsFile(mSettingsPath, mUser, false, false);
        else
            mUser[{ "Video", "window mode" }] = "0";
        // multisampling is unstable through mesa on xbox
        mUser[{ "Video", "antialiasing" }] = "0";
        mUser[{ "Shaders", "antialias alpha test" }] = "false";
        mLoaded = true;
    }
    catch (const std::exception&)
    {
    }
}

bool Uwp::SettingsStore::getBool(const char* category, const char* setting, bool fallback) const
{
    const std::string value = getValue(category, setting, fallback ? "true" : "false");
    return value == "true" || value == "1";
}

int Uwp::SettingsStore::getInt(const char* category, const char* setting, int fallback) const
{
    try
    {
        return std::stoi(getValue(category, setting, std::to_string(fallback)));
    }
    catch (const std::exception&)
    {
        return fallback;
    }
}

float Uwp::SettingsStore::getFloat(const char* category, const char* setting, float fallback) const
{
    try
    {
        return std::stof(getValue(category, setting, serializeFloat(fallback)));
    }
    catch (const std::exception&)
    {
        return fallback;
    }
}

std::string Uwp::SettingsStore::getString(const char* category, const char* setting, std::string fallback) const
{
    return getValue(category, setting, std::move(fallback));
}

void Uwp::SettingsStore::set(const char* category, const char* setting, bool value)
{
    mUser[{ category, setting }] = value ? "true" : "false";
}

void Uwp::SettingsStore::set(const char* category, const char* setting, int value)
{
    mUser[{ category, setting }] = std::to_string(value);
}

void Uwp::SettingsStore::set(const char* category, const char* setting, float value)
{
    mUser[{ category, setting }] = serializeFloat(value);
}

void Uwp::SettingsStore::set(const char* category, const char* setting, std::string_view value)
{
    mUser[{ category, setting }] = value;
}

void Uwp::SettingsStore::set(const char* category, const char* setting, const char* value)
{
    mUser[{ category, setting }] = value;
}

bool Uwp::SettingsStore::save()
{
    if (!mLoaded)
        return false;
    try
    {
        std::filesystem::create_directories(mSettingsPath.parent_path());
        Settings::SettingsFileParser parser;
        parser.saveSettingsFile(mSettingsPath, mUser);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::string Uwp::SettingsStore::serializeFloat(float value)
{
    std::ostringstream stream;
    stream.precision(std::numeric_limits<float>::max_digits10);
    stream << value;
    return stream.str();
}

std::string Uwp::SettingsStore::getValue(const char* category, const char* setting, std::string fallback) const
{
    const auto key = std::make_pair(std::string(category), std::string(setting));
    const auto user = mUser.find(key);
    if (user != mUser.end())
        return user->second;
    const auto defaults = mDefaults.find(key);
    return defaults != mDefaults.end() ? defaults->second : std::move(fallback);
}

const char* Uwp::settingsTabLabel(int tab)
{
    static constexpr const char* labels[settingsTabCount]
        = { "Display", "Gameplay", "Visuals", "Audio", "Interface", "Misc", "Input", "Language" };
    return tab >= 0 && tab < settingsTabCount ? labels[tab] : "";
}

void Uwp::drawSettingsPage(SettingsStore& settings, ModConfig& config, int tab, bool focus)
{
    switch (tab)
    {
        case 0:
            drawDisplaySettings(settings, focus);
            break;
        case 1:
            drawGameplaySettings(settings, focus);
            break;
        case 2:
            drawVisualSettings(settings, focus);
            break;
        case 3:
            drawAudioSettings(settings, focus);
            break;
        case 4:
            drawInterfaceSettings(settings, focus);
            break;
        case 5:
            drawMiscSettings(settings, focus);
            break;
        case 6:
            drawInputSettings(settings, focus);
            break;
        case 7:
            drawLanguageSettings(settings, config, focus);
            break;
    }
}
