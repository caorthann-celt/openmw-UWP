#include "uwp_setup_wizard.hpp"

#include "uwp_mod_config.hpp"

#include <imgui.h>

#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <apps/mwiniimporter/importer.hpp>
#include <components/files/conversion.hpp>
#include <components/toutf8/toutf8.hpp>

#include "../compat/uwp_content.hpp"

namespace Uwp
{
    namespace
    {
        bool hasFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::is_regular_file(path, error);
        }

        void drawFileStatus(const char* label, const std::filesystem::path& path, bool present)
        {
            ImGui::TextColored(present ? ImVec4(0.70f, 0.80f, 0.45f, 1.0f) : ImVec4(0.90f, 0.45f, 0.35f, 1.0f),
                "%s  %s", present ? "Found" : "Missing", label);
            ImGui::TextDisabled("%s", Files::pathToUnicodeString(path).c_str());
        }

        bool drawPrimaryButton(const char* label, bool requestFocus, bool enabled, bool& selected)
        {
            if (requestFocus)
                ImGui::SetKeyboardFocusHere();
            if (selected && enabled)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
            const bool pressed = ImGui::Button(label, ImVec2(150.0f, 42.0f));
            if (selected && enabled)
                ImGui::PopStyleColor();
            selected = ImGui::IsItemFocused();
            return pressed;
        }
    }

    SetupWizard::SetupWizard(std::filesystem::path localState)
        : mLocalState(std::move(localState))
    {
        refreshInstallation();
    }

    void SetupWizard::open()
    {
        mPage = Page::installation;
        mMessage.clear();
        mFocus = true;
        mPrimarySelected = true;
        mBackupCreated = false;
        refreshInstallation();
    }

    bool SetupWizard::back()
    {
        if (mPage == Page::installation || mPage == Page::complete)
            return false;
        mPage = static_cast<Page>(static_cast<int>(mPage) - 1);
        mFocus = true;
        mPrimarySelected = true;
        return true;
    }

    void SetupWizard::refreshInstallation()
    {
        mData = Files::getUwpGameDataPath();
        mRoot = mData.parent_path();
        mIni = mRoot / "Morrowind.ini";
        mHasIni = hasFile(mIni);
        mHasMaster = hasFile(mData / "Morrowind.esm");
        mHasArchive = hasFile(mData / "Morrowind.bsa");
    }

    void SetupWizard::next()
    {
        if (mPage != Page::complete)
            mPage = static_cast<Page>(static_cast<int>(mPage) + 1);
        mFocus = true;
        mPrimarySelected = true;
    }

    const char* SetupWizard::pageName() const
    {
        switch (mPage)
        {
            case Page::installation:
                return "Find Installation";
            case Page::import:
                return "Import Settings";
            case Page::review:
                return "Ready to Import";
            case Page::complete:
                return "Setup Complete";
        }
        return {};
    }

    bool SetupWizard::apply(ModConfig& config)
    {
        const std::filesystem::path configPath = mLocalState / "openmw.cfg";
        const std::filesystem::path backupPath = mLocalState / "openmw.cfg.bak";
        const std::filesystem::path temporaryPath = mLocalState / "openmw.cfg.tmp";

        try
        {
            mBackupCreated = false;
            if (hasFile(configPath))
            {
                std::filesystem::copy_file(configPath, backupPath, std::filesystem::copy_options::overwrite_existing);
                mBackupCreated = true;
            }

            MwIniImporter importer;
            MwIniImporter::multistrmap cfg = MwIniImporter::loadCfgFile(configPath);
            std::string encoding = "win1252";
            const auto currentEncoding = cfg.find("encoding");
            if (currentEncoding != cfg.end() && !currentEncoding->second.empty())
                encoding = currentEncoding->second.back();
            importer.setInputEncoding(ToUTF8::calculateEncoding(encoding));
            MwIniImporter::multistrmap ini = importer.loadIniFile(mIni);
            cfg["encoding"] = { encoding };

            if (mImportSettings)
            {
                if (!mImportFonts)
                {
                    ini.erase("Fonts:Font 0");
                    ini.erase("Fonts:Font 1");
                    ini.erase("Fonts:Font 2");
                }
                importer.merge(cfg, ini);
                importer.mergeFallback(cfg, ini);
            }

            importer.importArchives(cfg, ini);

            if (mImportAddons)
            {
                const auto data = cfg.find("data");
                const bool hadData = data != cfg.end();
                const std::vector<std::string> previousData = hadData ? data->second : std::vector<std::string>{};
                cfg["data"] = { Files::pathToUnicodeString(mData) };
                importer.importGameFiles(cfg, ini, mIni);
                if (hadData)
                    cfg["data"] = previousData;
                else
                    cfg.erase("data");
            }
            else if (cfg.find("content") == cfg.end() || cfg["content"].empty())
                cfg["content"] = { "Morrowind.esm" };

            {
                std::ofstream stream(temporaryPath, std::ios::trunc);
                if (!stream)
                    throw std::runtime_error("could not create the new configuration");
                MwIniImporter::writeToFile(stream, cfg);
                if (!stream)
                    throw std::runtime_error("could not write the new configuration");
            }

            std::error_code error;
            std::filesystem::remove(configPath, error);
            error.clear();
            std::filesystem::rename(temporaryPath, configPath, error);
            if (error)
            {
                if (hasFile(backupPath))
                    std::filesystem::copy_file(
                        backupPath, configPath, std::filesystem::copy_options::overwrite_existing);
                throw std::runtime_error("could not replace the configuration");
            }

            if (!config.load())
                throw std::runtime_error("could not reload the configuration");
            return true;
        }
        catch (const std::exception& error)
        {
            std::error_code ignored;
            std::filesystem::remove(temporaryPath, ignored);
            mMessage = error.what();
            return false;
        }
    }

    bool SetupWizard::draw(ModConfig& config)
    {
        constexpr float width = 960.0f;
        constexpr float height = 650.0f;
        const bool applyFocus = std::exchange(mFocus, false);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX((available.x - width) * 0.5f);
        ImGui::BeginChild(
            "setup wizard", ImVec2(width, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextUnformatted(pageName());
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 12.0f));

        if (mPage == Page::installation)
        {
            ImGui::TextWrapped(
                "The wizard looks for a Morrowind installation in External Storage first, "
                "then Internal Storage");
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            ImGui::Text("Using %s", mRoot == Files::getUwpExternalRootPath() ? "External Storage" : "Internal Storage");
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            drawFileStatus("Morrowind.ini", mIni, mHasIni);
            drawFileStatus("Morrowind.esm", mData / "Morrowind.esm", mHasMaster);
            drawFileStatus("Morrowind.bsa", mData / "Morrowind.bsa", mHasArchive);
            if (!mHasIni)
                ImGui::TextWrapped("Copy Morrowind.ini beside the data folder, then reopen the wizard");
        }
        else if (mPage == Page::import)
        {
            ImGui::TextWrapped(
                "OpenMW needs the original Morrowind settings to restore movies, weather, "
                "level up text and other game data");
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            ImGui::Checkbox("Import Settings From Morrowind.ini", &mImportSettings);
            ImGui::Checkbox("Import Add-on and Plugin Selection", &mImportAddons);
            ImGui::BeginDisabled(!mImportSettings);
            ImGui::Checkbox("Import Bitmap Fonts Setup From Morrowind.ini", &mImportFonts);
            ImGui::EndDisabled();
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            ImGui::TextWrapped(
                "Original bitmap fonts can look blurry when the interface is scaled. Leave this "
                "unticked to keep OpenMW's TrueType fonts");
        }
        else if (mPage == Page::review)
        {
            ImGui::Text("Installation: %s", Files::pathToUnicodeString(mRoot).c_str());
            ImGui::Text("Data: %s", Files::pathToUnicodeString(mData).c_str());
            ImGui::Separator();
            ImGui::Text("%s  Original settings", mImportSettings ? "Yes" : "No");
            ImGui::Text("%s  Add-ons and plugins", mImportAddons ? "Yes" : "No");
            ImGui::Text("%s  Bitmap fonts", mImportSettings && mImportFonts ? "Yes" : "No");
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            if (hasFile(mLocalState / "openmw.cfg"))
                ImGui::TextWrapped("The current openmw.cfg will be backed up before anything is changed");
            else
                ImGui::TextWrapped("A new openmw.cfg will be created in LocalState");
            if (!mMessage.empty())
                ImGui::TextColored(ImVec4(0.90f, 0.45f, 0.35f, 1.0f), "%s", mMessage.c_str());
        }
        else
        {
            ImGui::TextWrapped("Morrowind.ini has been imported");
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            if (mBackupCreated)
                ImGui::TextDisabled("A backup was saved as LocalState\\openmw.cfg.bak");
        }

        const float buttonY = ImGui::GetWindowHeight() - 66.0f;
        ImGui::SetCursorPosY(buttonY);
        if (mPage == Page::complete)
        {
            const bool done = drawPrimaryButton("Done", applyFocus, true, mPrimarySelected);
            ImGui::EndChild();
            return done;
        }

        if (mPage != Page::installation)
        {
            if (ImGui::Button("Back", ImVec2(150.0f, 42.0f)))
                back();
            ImGui::SameLine();
        }
        const bool ready = mHasIni && mHasMaster && mHasArchive;
        ImGui::BeginDisabled(!ready);
        const char* nextLabel = mPage == Page::review ? "Apply" : "Next";
        const bool pressed = drawPrimaryButton(nextLabel, applyFocus, ready, mPrimarySelected);
        if (pressed)
        {
            if (mPage == Page::review)
            {
                mMessage.clear();
                if (apply(config))
                    next();
            }
            else
                next();
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        return false;
    }
}
