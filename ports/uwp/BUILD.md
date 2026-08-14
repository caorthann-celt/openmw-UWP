# Building OpenMW for Xbox UWP

The build is fairly straightforward. CMake builds OpenMW and creates the Visual Studio solution, then Visual Studio packages it as an x64 UWP app.

SDL2, Mesa and LuaJIT are included in the repository. The only extra dependency folder you need is vcpkg.

This is an Xbox Series S and Series X build. It uses Mesa for desktop OpenGL, so Xbox One is not supported.

## What you need

- Visual Studio 2022 with C++ UWP support and the Windows 10 SDK
- CMake
- Git
- vcpkg with the `x64-uwp-static-md` triplet
- An internet connection for the first build

There are no Git submodules to worry about. A normal clone is enough.

### Visual Studio

Use the Visual Studio Installer to add the C++ UWP tools. The project uses the v143 toolset and Windows SDK `10.0.19041.0` by default.

### CMake and Git

Install both with Winget if you do not have them already:

```powershell
winget install Kitware.CMake
winget install Git.Git
```

Open a fresh PowerShell window afterwards and check they are available:

```powershell
Get-Command cmake.exe
Get-Command git.exe
```

### vcpkg

Keep vcpkg outside the OpenMW folder. Clone it and run the bootstrap script:

```powershell
git clone https://github.com/microsoft/vcpkg.git <vcpkg-path>
& "<vcpkg-path>\bootstrap-vcpkg.bat"
```

## Get the source

Clone this repository and enter its root:

```powershell
git clone https://github.com/caorthann-celt/OpenMW-UWP.git OpenMW
cd OpenMW
```

From here on, use a Developer PowerShell for Visual Studio 2022 opened in the OpenMW folder.

Set your vcpkg path and install the UWP libraries:

```powershell
$env:VCPKG_ROOT = "<vcpkg-path>"
$overlay = (Resolve-Path ".\ports\uwp\vcpkg-ports").Path

& "$env:VCPKG_ROOT\vcpkg.exe" install `
    "boost-program-options:x64-uwp-static-md" `
    "bullet3[double-precision]:x64-uwp-static-md" `
    "collada-dom:x64-uwp-static-md" `
    "ffmpeg[avcodec,avformat,swresample,swscale]:x64-uwp-static-md" `
    "freetype:x64-uwp-static-md" `
    "icu:x64-uwp-static-md" `
    "libjpeg-turbo:x64-uwp-static-md" `
    "libpng:x64-uwp-static-md" `
    "lz4:x64-uwp-static-md" `
    "openal-soft:x64-uwp-static-md" `
    "--overlay-ports=$overlay"
```

The first run can take a fair while because FFmpeg and ICU are built from source. Once they are done, vcpkg will reuse them on later builds.

These quick checks are handy before starting the main build:

```powershell
Test-Path "$env:VCPKG_ROOT\installed\x64-uwp-static-md\lib\collada-dom2.5-dp-vc140-mt.lib"
Test-Path "$env:VCPKG_ROOT\installed\x64-uwp-static-md\lib\avcodec.lib"
Test-Path ".\ports\uwp\dependencies\x64\lib\SDL2.lib"
Test-Path ".\ports\uwp\dependencies\x64\lib\lua51.lib"
```

You should get `True` four times.

## Build OpenMW

Once vcpkg is ready, the Release build is one command from the repository root:

```powershell
.\ports\uwp\build-engine.ps1 -Configuration Release
```

The first build downloads the pinned OpenSceneGraph, Dear ImGui, MyGUI, Recast, SQLite and yaml-cpp sources. They are kept under `ports\uwp\build`, so later builds do not download them again.

The Release solution and build output land here:

```text
ports\uwp\build\release
```

If you need symbols for debugging, use RelWithDebInfo instead:

```powershell
.\ports\uwp\build-engine.ps1 -Configuration RelWithDebInfo
```

That build is kept separately under `ports\uwp\build\relwithdebinfo`.

## Build or deploy from Visual Studio

Open the generated solution from that same PowerShell window:

```powershell
devenv .\ports\uwp\build\release\OpenMW.sln
```

For a RelWithDebInfo build, open `ports\uwp\build\relwithdebinfo\OpenMW.sln` instead and select `RelWithDebInfo` with `x64`.

Select `Release` and `x64`, then set `openmw` as the startup project. You can now build the MSIX locally or deploy it straight to an Xbox in Dev Mode.

The finished package is written below:

```text
ports\uwp\build\release\apps\openmw\AppPackages\openmw
```

RelWithDebInfo packages use the same path under `ports\uwp\build\relwithdebinfo`.

That folder contains the MSIX and the files needed to install it.

The package includes the OpenMW resources, launcher, SDL2, Mesa, libuwp and LuaJIT. SDL2 skips Game Bar setup on Xbox. Mesa is based on [SternXD's UWP 26.2.0 release](https://github.com/SternXD/mesa-uwp/releases/tag/26.2.0) with Daniel Worley's [libuwp framebuffer bridge](https://github.com/worleydl/mesa-uwp/commit/7a7c5d03ae3dad26717671cf2c7ce48c72ff23cc), and LuaJIT is pinned to official commit `4886b676a698acc4bbdf54adfabb3e33a8c020e8`.

The manifest enables code generation for LuaJIT and external storage access for game data. Original Morrowind files are not included and should never be added to the package.
