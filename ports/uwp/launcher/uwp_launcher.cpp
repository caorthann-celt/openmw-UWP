#include "uwp_launcher.hpp"
#include "uwp_folder_browser.hpp"
#include "uwp_launcher_settings.hpp"
#include "uwp_mod_config.hpp"
#include "uwp_setup_wizard.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <components/files/conversion.hpp>

namespace
{
    using Microsoft::WRL::ComPtr;
    using Uwp::SettingsStore;

    constexpr int sLogicalWidth = 1920;
    constexpr int sLogicalHeight = 1080;

    std::pair<int, int> launcherSurfaceSize()
    {
        try
        {
            const auto display = Windows::Graphics::Display::Core::HdmiDisplayInformation::GetForCurrentView();
            if (display)
            {
                const auto mode = display->GetCurrentDisplayMode();
                const int width = mode->ResolutionWidthInRawPixels;
                const int height = mode->ResolutionHeightInRawPixels;
                if (width > 0 && height > 0)
                    return { width, height };
            }
        }
        catch (...)
        {
        }

        return { sLogicalWidth, sLogicalHeight };
    }

    // launcher assets and state
    struct Texture
    {
        GLuint mId = 0;
        int mWidth = 0;
        int mHeight = 0;

        void clear()
        {
            if (mId != 0)
                glDeleteTextures(1, &mId);
            mId = 0;
        }
    };

    struct ControllerIcons
    {
        Texture mA;
        Texture mB;
        Texture mMenu;
        Texture mDpad;
        Texture mLeftShoulder;
        Texture mRightShoulder;

        void clear()
        {
            mRightShoulder.clear();
            mLeftShoulder.clear();
            mDpad.clear();
            mMenu.clear();
            mB.clear();
            mA.clear();
        }
    };

    struct MarqueeState
    {
        ImGuiID mRow = 0;
        double mStart = 0.0;
    };

    enum class Screen
    {
        main,
        setup,
        modding,
        settings,
        folderBrowser
    };

    enum class ModTab
    {
        data,
        content,
        archives
    };

    // shared launcher helpers
    const char* screenTitle(Screen screen)
    {
        switch (screen)
        {
            case Screen::modding:
                return "MODDING";
            case Screen::settings:
                return "SETTINGS";
            case Screen::setup:
                return "SETUP WIZARD";
            case Screen::folderBrowser:
                return "ADD DATA DIRECTORY";
            default:
                return "OPENMW";
        }
    }

    std::string toUtf8(std::wstring_view value)
    {
        if (value.empty())
            return {};
        const int size = WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        if (size > 0)
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::filesystem::path executableDirectory()
    {
        wchar_t modulePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
            return {};
        return std::filesystem::path(modulePath).parent_path();
    }

    bool loadTexture(const std::filesystem::path& path, Texture& texture)
    {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))))
            return false;

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(
                path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())))
            return false;

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
            return false;

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))
            || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
                0.0, WICBitmapPaletteTypeCustom)))
            return false;

        UINT width = 0;
        UINT height = 0;
        if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
            return false;

        const UINT stride = width * 4;
        std::vector<BYTE> pixels(static_cast<std::size_t>(stride) * height);
        if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())))
            return false;

        glGenTextures(1, &texture.mId);
        glBindTexture(GL_TEXTURE_2D, texture.mId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        texture.mWidth = static_cast<int>(width);
        texture.mHeight = static_cast<int>(height);
        return true;
    }

    ImTextureID textureId(const Texture& texture)
    {
        return static_cast<ImTextureID>(texture.mId);
    }

    // launcher theme
    void drawBackground(const Texture& background)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (background.mId != 0)
        {
            const float imageAspect = static_cast<float>(background.mWidth) / background.mHeight;
            const float displayAspect = display.x / display.y;
            ImVec2 uv0(0.0f, 0.0f);
            ImVec2 uv1(1.0f, 1.0f);
            if (imageAspect > displayAspect)
            {
                const float visible = displayAspect / imageAspect;
                uv0.x = (1.0f - visible) * 0.5f;
                uv1.x = 1.0f - uv0.x;
            }
            else
            {
                const float visible = imageAspect / displayAspect;
                uv0.y = (1.0f - visible) * 0.5f;
                uv1.y = 1.0f - uv0.y;
            }
            drawList->AddImage(textureId(background), ImVec2(0.0f, 0.0f), display, uv0, uv1);
        }
    }

    void drawLogo(const Texture& logo)
    {
        if (logo.mId != 0)
        {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            constexpr float padding = 28.0f;
            const float width = std::min(280.0f, display.x * 0.3f);
            const float height = width * logo.mHeight / logo.mWidth;
            ImGui::GetForegroundDrawList()->AddImage(
                textureId(logo), ImVec2(padding, padding), ImVec2(padding + width, padding + height));
        }
    }

    void drawShadowedText(ImDrawList* drawList, const ImVec2& position, const char* text)
    {
        drawList->AddText(ImVec2(position.x + 2.0f, position.y + 2.0f), IM_COL32(225, 210, 175, 200), text);
        drawList->AddText(position, IM_COL32(64, 52, 35, 255), text);
    }

    void drawHintText(ImDrawList* drawList, const ImVec2& position, const char* text)
    {
        drawList->AddText(ImVec2(position.x + 2.0f, position.y + 2.0f), IM_COL32(0, 0, 0, 220), text);
        drawList->AddText(position, IM_COL32(210, 190, 140, 255), text);
    }

    void drawHint(ImDrawList* drawList, float& x, float y, const Texture& icon, const char* fallback, const char* text)
    {
        constexpr float iconSize = 24.0f;
        if (icon.mId != 0)
        {
            drawList->AddImage(textureId(icon), ImVec2(x, y), ImVec2(x + iconSize, y + iconSize), ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f), IM_COL32(204, 181, 137, 255));
            x += iconSize + 5.0f;
        }
        else
        {
            drawHintText(drawList, ImVec2(x, y + 2.0f), fallback);
            x += ImGui::CalcTextSize(fallback).x + 5.0f;
        }
        drawHintText(drawList, ImVec2(x, y + 2.0f), text);
        x += ImGui::CalcTextSize(text).x + 18.0f;
    }

    void applyLauncherStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.FramePadding = ImVec2(12.0f, 8.0f);
        style.ItemSpacing = ImVec2(10.0f, 10.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.047f, 0.055f, 0.071f, 0.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.047f, 0.039f, 0.84f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.047f, 0.039f, 0.94f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.627f, 0.510f, 0.235f, 0.75f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.125f, 0.102f, 0.071f, 0.90f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.251f, 0.204f, 0.137f, 0.95f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.392f, 0.322f, 0.180f, 0.95f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.251f, 0.204f, 0.137f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.392f, 0.322f, 0.180f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.510f, 0.420f, 0.196f, 1.0f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.906f, 0.745f, 0.310f, 1.0f);
        style.Colors[ImGuiCol_CheckboxSelectedBg] = style.Colors[ImGuiCol_Button];
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.706f, 0.580f, 0.255f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.906f, 0.745f, 0.310f, 1.0f);
        style.Colors[ImGuiCol_Header] = style.Colors[ImGuiCol_Button];
        style.Colors[ImGuiCol_HeaderHovered] = style.Colors[ImGuiCol_ButtonHovered];
        style.Colors[ImGuiCol_HeaderActive] = style.Colors[ImGuiCol_ButtonActive];
        style.Colors[ImGuiCol_Tab] = ImVec4(0.125f, 0.102f, 0.071f, 0.94f);
        style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_ButtonHovered];
        style.Colors[ImGuiCol_TabSelected] = style.Colors[ImGuiCol_Button];
        style.Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.906f, 0.745f, 0.310f, 1.0f);
        style.Colors[ImGuiCol_TabDimmed] = style.Colors[ImGuiCol_Tab];
        style.Colors[ImGuiCol_TabDimmedSelected] = style.Colors[ImGuiCol_TabSelected];
        style.Colors[ImGuiCol_TabDimmedSelectedOverline] = style.Colors[ImGuiCol_TabSelectedOverline];
        style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
        style.Colors[ImGuiCol_SeparatorHovered] = style.Colors[ImGuiCol_ButtonHovered];
        style.Colors[ImGuiCol_SeparatorActive] = style.Colors[ImGuiCol_ButtonActive];
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.055f, 0.047f, 0.039f, 0.70f);
        style.Colors[ImGuiCol_ScrollbarGrab] = style.Colors[ImGuiCol_Button];
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = style.Colors[ImGuiCol_ButtonHovered];
        style.Colors[ImGuiCol_ScrollbarGrabActive] = style.Colors[ImGuiCol_ButtonActive];
        style.Colors[ImGuiCol_NavCursor] = ImVec4(0.906f, 0.745f, 0.310f, 1.0f);
        style.ScaleAllSizes(1.5f);
    }

    // mod list controls
    void clampSelection(int& selection, int size)
    {
        if (size <= 0)
            selection = -1;
        else if (selection >= size)
            selection = size - 1;
    }

    float sideButtonWidth()
    {
        return std::max(
            170.0f, ImGui::CalcTextSize("Add This Directory").x + ImGui::GetStyle().FramePadding.x * 2.0f + 24.0f);
    }

    float sidePanelWidth()
    {
        return sideButtonWidth() + 48.0f;
    }

    bool drawSideButton(const char* label, float panelMin, float panelMax, bool repeat = false)
    {
        const float width = sideButtonWidth();
        ImGui::SetCursorScreenPos(
            ImVec2(panelMin + std::max(0.0f, (panelMax - panelMin - width) * 0.5f), ImGui::GetCursorScreenPos().y));
        ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, repeat);
        const bool pressed = ImGui::Button(label, ImVec2(width, 42.0f));
        ImGui::PopItemFlag();
        return pressed;
    }

    // friendly paths for internal storage
    std::string formatDataPath(const Uwp::ModEntry& entry, const std::filesystem::path& localState)
    {
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(entry.mPath, localState, error);
        if (!error && !relative.empty() && *relative.begin() != std::filesystem::path(L".."))
        {
            std::filesystem::path display = L"LocalState";
            if (relative != std::filesystem::path(L"."))
                display /= relative;
            return Files::pathToUnicodeString(display);
        }
        return Files::pathToUnicodeString(entry.mPath);
    }

    bool drawModEntryList(const char* id, std::vector<Uwp::ModEntry>& entries, int& selection, bool showCheckboxes,
        const std::filesystem::path& localState, MarqueeState& marquee, const ImVec2& size, bool& requestFocus,
        bool& requestScroll)
    {
        bool entryPressed = false;
        if (ImGui::BeginListBox(id, size))
        {
            const int focusIndex = selection >= 0 ? selection : 0;
            bool marqueeFocused = false;
            float orderColumnWidth = 0.0f;
            for (int index = 0; index < static_cast<int>(entries.size()); ++index)
            {
                const std::string value = std::to_string(index + 1) + ".";
                orderColumnWidth = std::max(orderColumnWidth, ImGui::CalcTextSize(value.c_str()).x);
            }
            for (int index = 0; index < static_cast<int>(entries.size()); ++index)
            {
                if (requestFocus && index == focusIndex)
                    ImGui::SetKeyboardFocusHere();
                const std::string order = std::to_string(index + 1) + ".";
                std::string label;
                if (!entries[index].mPresent)
                    label += "Missing  ";
                label += showCheckboxes ? toUtf8(entries[index].mName) : formatDataPath(entries[index], localState);
                const std::string rowId = "##entry" + std::to_string(index);
                const bool pressed = ImGui::Selectable(rowId.c_str(), selection == index,
                    ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, ImGui::GetTextLineHeight()));
                const bool focused = ImGui::IsItemFocused();
                const ImGuiID itemId = ImGui::GetItemID();
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const ImVec2 rowMax = ImGui::GetItemRectMax();
                const float rowHeight = rowMax.y - rowMin.y;
                float x = rowMin.x;
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImVec2 orderSize = ImGui::CalcTextSize(order.c_str());
                drawList->AddText(
                    ImVec2(x + orderColumnWidth - orderSize.x, rowMin.y + (rowHeight - orderSize.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), order.c_str());
                x += orderColumnWidth + ImGui::GetStyle().ItemInnerSpacing.x;
                if (showCheckboxes)
                {
                    const float checkboxSize = std::max(12.0f, ImGui::GetTextLineHeight() - 4.0f);
                    const ImVec2 checkboxMin(x, rowMin.y + (rowHeight - checkboxSize) * 0.5f);
                    const ImVec2 checkboxMax(checkboxMin.x + checkboxSize, checkboxMin.y + checkboxSize);
                    const ImGuiCol checkboxColor = entries[index].mEnabled ? ImGuiCol_CheckboxSelectedBg
                        : selection == index                               ? ImGuiCol_Header
                                                                           : ImGuiCol_FrameBg;
                    ImGui::RenderFrame(checkboxMin, checkboxMax, ImGui::GetColorU32(checkboxColor), true,
                        ImGui::GetStyle().FrameRounding);
                    drawList->AddRect(
                        checkboxMin, checkboxMax, ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetStyle().FrameRounding);
                    if (entries[index].mEnabled)
                    {
                        const float checkPadding = std::max(1.0f, std::floor(checkboxSize / 6.0f));
                        ImGui::RenderCheckMark(drawList,
                            ImVec2(checkboxMin.x + checkPadding, checkboxMin.y + checkPadding),
                            ImGui::GetColorU32(ImGuiCol_CheckMark), checkboxSize - checkPadding * 2.0f);
                    }
                    x = checkboxMax.x + ImGui::GetStyle().ItemInnerSpacing.x;
                }
                // marquee for long mod paths
                float offset = 0.0f;
                const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
                if (!showCheckboxes && focused)
                {
                    marqueeFocused = true;
                    if (labelSize.x <= rowMax.x - x)
                        marquee.mRow = 0;
                    else
                    {
                        if (marquee.mRow != itemId)
                        {
                            marquee.mRow = itemId;
                            marquee.mStart = ImGui::GetTime();
                        }
                        constexpr double delay = 0.75;
                        constexpr double pause = 0.75;
                        constexpr float speed = 50.0f;
                        const float overflow = labelSize.x - (rowMax.x - x);
                        const double travel = overflow / speed;
                        const double phase = std::fmod(ImGui::GetTime() - marquee.mStart, delay + travel + pause);
                        if (phase > delay)
                            offset = phase < delay + travel ? static_cast<float>((phase - delay) * speed) : overflow;
                    }
                }
                drawList->PushClipRect(ImVec2(x, rowMin.y), rowMax, true);
                drawList->AddText(ImVec2(x - offset, rowMin.y + (rowHeight - labelSize.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
                drawList->PopClipRect();
                if (pressed)
                {
                    selection = index;
                    entryPressed = true;
                }
                if (requestScroll && index == selection)
                    ImGui::SetScrollHereY(0.5f);
            }
            if (showCheckboxes || !marqueeFocused)
                marquee.mRow = 0;
            requestFocus = false;
            requestScroll = false;
            ImGui::EndListBox();
        }
        return entryPressed;
    }

    void drawTabStrip(const char* const labels[], int count, int selectedTab)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float tabWidth = width / count;
        const float height = ImGui::GetTextLineHeight() + 16.0f;
        drawList->AddRectFilled(start, ImVec2(start.x + width, start.y + height), ImGui::GetColorU32(ImGuiCol_Tab));
        for (int index = 0; index < count; ++index)
        {
            const float x = start.x + tabWidth * index;
            const ImVec2 textSize = ImGui::CalcTextSize(labels[index]);
            const ImU32 textColor
                = index == selectedTab ? IM_COL32(230, 196, 108, 255) : ImGui::GetColorU32(ImGuiCol_Text);
            drawList->AddText(ImVec2(x + (tabWidth - textSize.x) * 0.5f, start.y + (height - textSize.y) * 0.5f),
                textColor, labels[index]);
            if (index == selectedTab)
                drawList->AddRectFilled(ImVec2(x, start.y + height - 3.0f), ImVec2(x + tabWidth, start.y + height),
                    ImGui::GetColorU32(ImGuiCol_TabSelectedOverline));
        }
        ImGui::Dummy(ImVec2(0.0f, height));
    }

    void drawSettingsScreen(SettingsStore& settings, Uwp::ModConfig& config, int selectedTab, bool& focusSettings,
        const ControllerIcons& icons)
    {
        const char* tabs[Uwp::settingsTabCount] = {};
        for (int index = 0; index < Uwp::settingsTabCount; ++index)
            tabs[index] = Uwp::settingsTabLabel(index);
        ImGui::BeginChild("settings", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - 34.0f), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float hintX = ImGui::GetCursorScreenPos().x;
        const float hintY = ImGui::GetCursorScreenPos().y;
        drawHint(ImGui::GetWindowDrawList(), hintX, hintY, icons.mLeftShoulder, "LB", "Previous tab");
        const float rightHintWidth = icons.mRightShoulder.mId != 0 ? 24.0f + 5.0f + ImGui::CalcTextSize("Next tab").x
                                                                   : ImGui::CalcTextSize("RB Next tab").x;
        hintX = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x - rightHintWidth;
        drawHint(ImGui::GetWindowDrawList(), hintX, hintY, icons.mRightShoulder, "RB", "Next tab");
        ImGui::Dummy(ImVec2(0.0f, 26.0f));
        drawTabStrip(tabs, Uwp::settingsTabCount, selectedTab);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 14.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushID(selectedTab);
        ImGui::BeginChild("settings-options", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        Uwp::drawSettingsPage(settings, config, selectedTab, focusSettings);
        focusSettings = false;
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::EndChild();
    }

    // launcher pages
    bool drawMainButton(const char* label, const ImVec2& size, bool requestFocus, bool selected, bool& focused)
    {
        if (requestFocus)
            ImGui::SetKeyboardFocusHere();
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor();
        if (selected)
            ImGui::PopStyleColor();
        focused = ImGui::IsItemFocused();
        return pressed;
    }

    void drawMainScreen(Screen& screen, Uwp::SetupWizard& wizard, int& mainSelection, bool& focusMain,
        bool& focusModding, bool& focusModActions, bool& focusSettings, bool& running, bool& launch)
    {
        const float width = 320.0f;
        const float height = 5.0f * 48.0f + 4.0f * ImGui::GetStyle().ItemSpacing.y;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - width) * 0.5f);
        ImGui::SetCursorPosY((ImGui::GetWindowHeight() - height) * 0.5f);
        ImGui::BeginGroup();
        if (focusMain)
            mainSelection = 0;
        bool focused = false;
        if (drawMainButton("Start Game", ImVec2(width, 48.0f), focusMain, mainSelection == 0, focused))
            launch = true;
        if (focused)
            mainSelection = 0;
        focusMain = false;
        if (drawMainButton("Setup Wizard", ImVec2(width, 48.0f), false, mainSelection == 1, focused))
        {
            wizard.open();
            screen = Screen::setup;
        }
        if (focused)
            mainSelection = 1;
        if (drawMainButton("Modding", ImVec2(width, 48.0f), false, mainSelection == 2, focused))
        {
            screen = Screen::modding;
            focusModding = true;
            focusModActions = false;
        }
        if (focused)
            mainSelection = 2;
        if (drawMainButton("Settings", ImVec2(width, 48.0f), false, mainSelection == 3, focused))
        {
            screen = Screen::settings;
            focusSettings = true;
        }
        if (focused)
            mainSelection = 3;
        if (drawMainButton("Exit", ImVec2(width, 48.0f), false, mainSelection == 4, focused))
            running = false;
        if (focused)
            mainSelection = 4;
        ImGui::EndGroup();
    }

    void drawModdingScreen(Uwp::ModConfig& config, ModTab selectedTab, int& dataSelection, int& contentSelection,
        int& archiveSelection, bool& focusModding, bool& focusModActions, bool& scrollModSelection, bool& focusBrowser,
        Screen& screen, Uwp::FolderBrowser& browser, const std::filesystem::path& localState, MarqueeState& marquee,
        const ControllerIcons& icons)
    {
        static constexpr const char* tabs[] = { "Data Directories", "Content Files", "Archive Files" };
        const ImVec2 padding = ImGui::GetStyle().WindowPadding;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x, 0.0f));
        ImGui::BeginChild("modding", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - 34.0f),
            ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding.y);
        float hintX = ImGui::GetCursorScreenPos().x;
        const float hintY = ImGui::GetCursorScreenPos().y;
        drawHint(ImGui::GetWindowDrawList(), hintX, hintY, icons.mLeftShoulder, "LB", "Previous tab");
        const float rightHintWidth = icons.mRightShoulder.mId != 0 ? 24.0f + 5.0f + ImGui::CalcTextSize("Next tab").x
                                                                   : ImGui::CalcTextSize("RB Next tab").x;
        hintX = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x - rightHintWidth;
        drawHint(ImGui::GetWindowDrawList(), hintX, hintY, icons.mRightShoulder, "RB", "Next tab");
        ImGui::Dummy(ImVec2(0.0f, 26.0f));
        drawTabStrip(tabs, 3, static_cast<int>(selectedTab));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 0.0f));
        ImGui::BeginChild("modding-options", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened);
        ImGui::PopStyleVar();
        const float actionsWidth = sidePanelWidth();
        const float listWidth = ImGui::GetContentRegionAvail().x - actionsWidth - ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginGroup();
        const float listMin = ImGui::GetCursorScreenPos().x;
        const float headerHeight = ImGui::GetTextLineHeight() + 16.0f;
        const char* header = selectedTab == ModTab::data ? "Load order" : "Enabled and load order";
        const float headerY = ImGui::GetCursorPosY();
        const ImVec2 headerStart = ImGui::GetCursorScreenPos();
        const ImVec2 headerSize = ImGui::CalcTextSize(header);
        ImGui::GetWindowDrawList()->AddText(ImVec2(headerStart.x + ImGui::GetStyle().FramePadding.x,
                                                headerStart.y + (headerHeight - headerSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), header);
        ImGui::Dummy(ImVec2(listWidth, headerHeight));
        ImGui::SetCursorPosY(headerY + headerHeight);
        std::vector<Uwp::ModEntry>* entries = nullptr;
        int* selection = nullptr;
        if (selectedTab == ModTab::data)
        {
            entries = &config.dataDirectories();
            selection = &dataSelection;
        }
        else if (selectedTab == ModTab::content)
        {
            entries = &config.contentFiles();
            selection = &contentSelection;
        }
        else
        {
            entries = &config.archiveFiles();
            selection = &archiveSelection;
        }
        clampSelection(*selection, static_cast<int>(entries->size()));
        const bool entryPressed = drawModEntryList("##mod-list", *entries, *selection, selectedTab != ModTab::data,
            localState, marquee, ImVec2(listWidth, -0.001f), focusModding, scrollModSelection);
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        const float panelMin = listMin + listWidth;
        const float panelMax = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
        ImGui::Dummy(ImVec2(actionsWidth, headerHeight));
        if (selectedTab == ModTab::data)
        {
            if (focusModActions)
            {
                ImGui::SetKeyboardFocusHere();
                focusModActions = false;
            }
            if (drawSideButton("Add Directory", panelMin, panelMax))
            {
                browser.reset();
                screen = Screen::folderBrowser;
                focusBrowser = true;
                focusModActions = false;
            }
            if (drawSideButton("Remove", panelMin, panelMax))
            {
                config.removeDataDirectory(*selection);
                clampSelection(*selection, static_cast<int>(entries->size()));
            }
            if (drawSideButton("Move Up", panelMin, panelMax, true) && config.moveDataDirectory(*selection, -1))
            {
                --*selection;
                scrollModSelection = true;
            }
            if (drawSideButton("Move Down", panelMin, panelMax, true) && config.moveDataDirectory(*selection, 1))
            {
                ++*selection;
                scrollModSelection = true;
            }
        }
        else
        {
            if (focusModActions)
            {
                ImGui::SetKeyboardFocusHere();
                focusModActions = false;
            }
            const bool togglePressed = drawSideButton("Enable / Disable", panelMin, panelMax);
            if (entryPressed || togglePressed)
                config.toggleEntry(*entries, *selection);
            if (drawSideButton("Move Up", panelMin, panelMax, true) && config.moveEntry(*entries, *selection, -1))
            {
                --*selection;
                scrollModSelection = true;
            }
            if (drawSideButton("Move Down", panelMin, panelMax, true) && config.moveEntry(*entries, *selection, 1))
            {
                ++*selection;
                scrollModSelection = true;
            }
        }
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::EndChild();
    }

    void drawFolderBrowser(Uwp::FolderBrowser& browser, Uwp::ModConfig& config, int& browserSelection,
        int& dataSelection, bool& focusBrowser, bool& focusBrowserActions, bool& focusModding, Screen& screen)
    {
        ImGui::BeginChild("folder-browser", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - 34.0f),
            ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const std::string current
            = browser.currentPath().empty() ? "Choose storage" : Files::pathToUnicodeString(browser.currentPath());
        ImGui::TextUnformatted(current.c_str());
        const float actionsWidth = sidePanelWidth();
        const float listWidth = ImGui::GetContentRegionAvail().x - actionsWidth - ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginGroup();
        float listMax = ImGui::GetCursorScreenPos().x + listWidth;
        if (ImGui::BeginListBox("##folders", ImVec2(listWidth, -1.0f)))
        {
            const auto& entries = browser.entries();
            clampSelection(browserSelection, static_cast<int>(entries.size()));
            const bool applyFocus = focusBrowser;
            focusBrowser = false;
            for (int index = 0; index < static_cast<int>(entries.size()); ++index)
            {
                if (applyFocus && index == browserSelection)
                    ImGui::SetKeyboardFocusHere();
                const std::string label = toUtf8(entries[index].mName) + "##" + std::to_string(index);
                const bool pressed = ImGui::Selectable(label.c_str(), false);
                if (ImGui::IsItemFocused())
                    browserSelection = index;
                if (pressed)
                {
                    browserSelection = index;
                    if (browser.open(index))
                    {
                        browserSelection = 0;
                        focusBrowser = true;
                        focusBrowserActions = false;
                    }
                }
            }
            ImGui::EndListBox();
            listMax = ImGui::GetItemRectMax().x;
        }
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        const float panelMin = listMax;
        const float contentMax = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const float windowMax = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
        const float panelMax = (contentMax + windowMax) * 0.5f;
        if (focusBrowserActions)
        {
            ImGui::SetKeyboardFocusHere();
            focusBrowserActions = false;
        }
        if (drawSideButton("Open", panelMin, panelMax) && browser.open(browserSelection))
        {
            browserSelection = 0;
            focusBrowser = true;
            focusBrowserActions = false;
        }
        if (drawSideButton("Up", panelMin, panelMax) && browser.up(browserSelection))
        {
            focusBrowser = true;
        }
        if (!browser.currentPath().empty() && drawSideButton("Add This Directory", panelMin, panelMax))
        {
            if (config.addDataDirectory(browser.currentPath()))
                dataSelection = static_cast<int>(config.dataDirectories().size()) - 1;
            screen = Screen::modding;
            focusModding = true;
            focusBrowserActions = false;
        }
        if (drawSideButton("Cancel", panelMin, panelMax))
        {
            screen = Screen::modding;
            focusModding = true;
            focusBrowserActions = false;
        }
        ImGui::EndGroup();
        ImGui::EndChild();
    }

    // launcher frame
    void drawLauncher(Screen& screen, Uwp::ModConfig& config, Uwp::FolderBrowser& browser, Uwp::SetupWizard& wizard,
        SettingsStore& settings, ModTab selectedModTab, int selectedSettingsTab, int& mainSelection, int& dataSelection,
        int& contentSelection, int& archiveSelection, int& browserSelection, bool& focusMain, bool& focusModding,
        bool& focusBrowser, bool& focusModActions, bool& scrollModSelection, bool& focusBrowserActions,
        bool& focusSettings, const std::filesystem::path& localState, MarqueeState& marquee, const Texture& background,
        const Texture& logo, const ControllerIcons& icons, bool& running, bool& launch)
    {
        ImGuiIO& io = ImGui::GetIO();
        drawBackground(background);
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("OpenMW", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoScrollWithMouse);

        if (screen != Screen::main)
        {
            ImGui::SetCursorPosY(82.0f);
            const char* title = screenTitle(screen);
            const float titleWidth = ImGui::CalcTextSize(title).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - titleWidth) * 0.5f);
            drawShadowedText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), title);
            ImGui::Dummy(ImGui::CalcTextSize(title));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
        }

        if (screen == Screen::main)
            drawMainScreen(screen, wizard, mainSelection, focusMain, focusModding, focusModActions, focusSettings,
                running, launch);
        else if (screen == Screen::setup)
        {
            if (wizard.draw(config))
            {
                screen = Screen::main;
                focusMain = true;
            }
        }
        else if (screen == Screen::settings)
            drawSettingsScreen(settings, config, selectedSettingsTab, focusSettings, icons);
        else if (screen == Screen::modding)
            drawModdingScreen(config, selectedModTab, dataSelection, contentSelection, archiveSelection, focusModding,
                focusModActions, scrollModSelection, focusBrowser, screen, browser, localState, marquee, icons);
        else
            drawFolderBrowser(browser, config, browserSelection, dataSelection, focusBrowser, focusBrowserActions,
                focusModding, screen);

        float hintX = 20.0f;
        const float hintY = io.DisplaySize.y - 34.0f;
        drawHint(ImGui::GetWindowDrawList(), hintX, hintY, icons.mA, "A", "Select");
        drawHint(ImGui::GetWindowDrawList(), hintX, hintY, icons.mB, "B", screen == Screen::main ? "Exit" : "Back");
        if (screen != Screen::main && screen != Screen::setup)
            drawHint(ImGui::GetWindowDrawList(), hintX, hintY, icons.mMenu, "Menu", "Start");
        if (screen == Screen::modding || screen == Screen::folderBrowser)
            drawHint(ImGui::GetWindowDrawList(), hintX, hintY, icons.mDpad, "D-pad", "Switch column");
        ImGui::End();
        drawLogo(logo);
    }
}

// launcher setup and main loop
Uwp::LauncherResult Uwp::runLauncher(const std::filesystem::path& localState)
{
    ModConfig config(localState);
    FolderBrowser browser(localState);
    SetupWizard wizard(localState);
    SettingsStore settings(localState);
    if (!config.load())
        return LauncherResult::failed;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
        return LauncherResult::failed;

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    const auto [surfaceWidth, surfaceHeight] = launcherSurfaceSize();
    SDL_Window* window = SDL_CreateWindow("OpenMW", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, surfaceWidth,
        surfaceHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (!window)
    {
        SDL_Quit();
        return LauncherResult::failed;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context)
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return LauncherResult::failed;
    }

    SDL_GL_SetSwapInterval(1);
    const std::filesystem::path package = executableDirectory();
    // pick one background for this launch
    std::random_device randomDevice;
    std::mt19937 random(randomDevice());
    const int backgroundIndex = std::uniform_int_distribution<int>(1, 12)(random);
    const std::wstring backgroundName
        = L"background" + std::wstring(backgroundIndex < 10 ? L"0" : L"") + std::to_wstring(backgroundIndex) + L".jpg";
    Texture background;
    Texture logo;
    ControllerIcons icons;
    loadTexture(package / "launcher" / "backgrounds" / backgroundName, background);
    loadTexture(package / "launcher" / "mw_logo.png", logo);
    const std::filesystem::path textures = package / "resources" / "vfs" / "textures";
    loadTexture(textures / "omw_steam_button_a.dds", icons.mA);
    loadTexture(textures / "omw_steam_button_b.dds", icons.mB);
    loadTexture(textures / "omw_steam_button_menu.dds", icons.mMenu);
    loadTexture(textures / "omw_steam_button_dpad.dds", icons.mDpad);
    loadTexture(textures / "omw_xbox_button_lb.dds", icons.mLeftShoulder);
    loadTexture(textures / "omw_xbox_button_rb.dds", icons.mRightShoulder);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags
        |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;
    SDL_ShowCursor(SDL_DISABLE);
    // use the morrowind font with a fallback for missing glyphs
    const std::string fontPath
        = Files::pathToUnicodeString(package / "resources" / "vfs" / "fonts" / "MysticCards.ttf");
    if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 20.0f) != nullptr)
    {
        ImFontConfig fallback;
        fallback.MergeMode = true;
        fallback.SizePixels = 20.0f;
        io.Fonts->AddFontDefault(&fallback);
    }
    else
        io.Fonts->AddFontDefault();
    applyLauncherStyle();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL2_Init();

    const bool needsSetup = !std::filesystem::is_regular_file(localState / "openmw.cfg");
    Screen screen = needsSetup ? Screen::setup : Screen::main;
    if (needsSetup)
        wizard.open();
    ModTab selectedModTab = ModTab::data;
    int selectedSettingsTab = 0;
    int mainSelection = 0;
    int dataSelection = -1;
    int contentSelection = -1;
    int archiveSelection = -1;
    int browserSelection = 0;
    bool focusMain = true;
    bool focusModding = false;
    bool focusBrowser = false;
    bool focusModActions = false;
    bool scrollModSelection = false;
    bool focusBrowserActions = false;
    bool focusSettings = false;
    MarqueeState marquee;
    bool running = true;
    bool launch = false;
    const auto clearModSelection = [&]() {
        if (selectedModTab == ModTab::data)
            dataSelection = -1;
        else if (selectedModTab == ModTab::content)
            contentSelection = -1;
        else
            archiveSelection = -1;
    };
    const auto goBack = [&]() {
        if (screen == Screen::main)
            running = false;
        else if (screen == Screen::folderBrowser)
        {
            if (browser.currentPath().empty())
            {
                screen = Screen::modding;
                focusModding = true;
                focusBrowserActions = false;
            }
            else
            {
                browser.up(browserSelection);
                focusBrowser = true;
                focusBrowserActions = false;
            }
        }
        else if (screen == Screen::setup)
        {
            if (!wizard.back())
            {
                screen = Screen::main;
                focusMain = true;
            }
        }
        else
        {
            screen = Screen::main;
            focusMain = true;
            focusModActions = false;
        }
    };
    while (running && !launch)
    {
        // controller navigation
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                running = false;
            else if (event.type == SDL_KEYDOWN && !event.key.repeat)
            {
                switch (event.key.keysym.sym)
                {
                    case SDLK_ESCAPE:
                        goBack();
                        break;
                    default:
                        break;
                }
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN)
            {
                switch (event.cbutton.button)
                {
                    case SDL_CONTROLLER_BUTTON_B:
                        goBack();
                        break;
                    case SDL_CONTROLLER_BUTTON_START:
                        if (screen != Screen::setup)
                            launch = true;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        if (screen == Screen::modding)
                        {
                            focusModding = true;
                            focusModActions = false;
                        }
                        else if (screen == Screen::folderBrowser)
                        {
                            focusBrowser = true;
                            focusBrowserActions = false;
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        if (screen == Screen::modding)
                        {
                            focusModding = false;
                            focusModActions = true;
                        }
                        else if (screen == Screen::folderBrowser)
                        {
                            focusBrowser = false;
                            focusBrowserActions = true;
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                        if (screen == Screen::settings)
                        {
                            selectedSettingsTab
                                = (selectedSettingsTab + Uwp::settingsTabCount - 1) % Uwp::settingsTabCount;
                            focusSettings = true;
                        }
                        else if (screen == Screen::modding)
                        {
                            selectedModTab = static_cast<ModTab>((static_cast<int>(selectedModTab) + 2) % 3);
                            clearModSelection();
                            focusModding = true;
                            focusModActions = false;
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                        if (screen == Screen::settings)
                        {
                            selectedSettingsTab = (selectedSettingsTab + 1) % Uwp::settingsTabCount;
                            focusSettings = true;
                        }
                        else if (screen == Screen::modding)
                        {
                            selectedModTab = static_cast<ModTab>((static_cast<int>(selectedModTab) + 1) % 3);
                            clearModSelection();
                            focusModding = true;
                            focusModActions = false;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        // draw the next frame
        int drawableWidth;
        int drawableHeight;
        SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        io.DisplaySize = ImVec2(static_cast<float>(sLogicalWidth), static_cast<float>(sLogicalHeight));
        io.DisplayFramebufferScale = ImVec2(
            static_cast<float>(drawableWidth) / sLogicalWidth, static_cast<float>(drawableHeight) / sLogicalHeight);
        ImGui::NewFrame();
        drawLauncher(screen, config, browser, wizard, settings, selectedModTab, selectedSettingsTab, mainSelection,
            dataSelection, contentSelection, archiveSelection, browserSelection, focusMain, focusModding, focusBrowser,
            focusModActions, scrollModSelection, focusBrowserActions, focusSettings, localState, marquee, background,
            logo, icons, running, launch);
        ImGui::Render();

        glViewport(0, 0, drawableWidth, drawableHeight);
        glClearColor(0.047f, 0.055f, 0.071f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // release the launcher context before starting openmw
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    icons.clear();
    logo.clear();
    background.clear();
    glFinish();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!launch)
        return LauncherResult::cancel;
    return config.save() && settings.save() ? LauncherResult::launch : LauncherResult::failed;
}
