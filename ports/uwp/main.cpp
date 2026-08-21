#include <SDL_main.h>
#include <windows.h>

#include "compat/uwp_content.hpp"
#include "launcher/uwp_launcher.hpp"

#pragma warning(disable : 4447)

int main(int argc, char* argv[]);

static int RunOpenMW(int argc, char* argv[])
{
    const std::filesystem::path localState = Files::getUwpLocalStatePath();
    const Uwp::LauncherResult result = Uwp::runLauncher(localState);
    if (result == Uwp::LauncherResult::cancel)
        return 0;
    if (result == Uwp::LauncherResult::failed)
        OutputDebugStringA("OpenMW launcher failed\n");

    return main(argc, argv);
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    OutputDebugStringA("OpenMW UWP host entered\n");
    return SDL_WinRTRunApp(RunOpenMW, nullptr);
}
