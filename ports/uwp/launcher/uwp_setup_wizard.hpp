#ifndef OPENMW_PORTS_UWP_LAUNCHER_UWP_SETUP_WIZARD_HPP
#define OPENMW_PORTS_UWP_LAUNCHER_UWP_SETUP_WIZARD_HPP

#include <filesystem>
#include <string>

namespace Uwp
{
    class ModConfig;

    class SetupWizard
    {
    public:
        explicit SetupWizard(std::filesystem::path localState);

        void open();
        bool back();
        bool draw(ModConfig& config);

    private:
        enum class Page
        {
            installation,
            import,
            review,
            complete
        };

        void refreshInstallation();
        bool apply(ModConfig& config);
        void next();
        const char* pageName() const;

        std::filesystem::path mLocalState;
        std::filesystem::path mRoot;
        std::filesystem::path mData;
        std::filesystem::path mIni;
        Page mPage = Page::installation;
        bool mImportSettings = true;
        bool mImportAddons = true;
        bool mImportFonts = true;
        bool mFocus = true;
        bool mPrimarySelected = true;
        bool mHasIni = false;
        bool mHasMaster = false;
        bool mHasArchive = false;
        bool mBackupCreated = false;
        std::string mMessage;
    };
}

#endif
