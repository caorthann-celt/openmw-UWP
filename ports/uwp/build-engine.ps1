param(
    [string]$Configuration = "Release",
    [string]$WindowsSdkVersion = "10.0.19041.0"
)

$ErrorActionPreference = "Stop"

if (-not $env:VCPKG_ROOT) {
    throw "Set VCPKG_ROOT to the vcpkg checkout."
}

$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$buildRoot = Join-Path $PSScriptRoot "build/$($Configuration.ToLowerInvariant())"
$cmakeRoot = Join-Path $PSScriptRoot "cmake"
$dependencyRoot = Join-Path $PSScriptRoot "dependencies"
$toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
$vcpkgInstalled = Join-Path $env:VCPKG_ROOT "installed/x64-uwp-static-md"
$sdlInclude = Join-Path $dependencyRoot "x64/include/SDL2"
$sdlLibrary = Join-Path $dependencyRoot "x64/lib/SDL2.lib"
$runtimeDir = Join-Path $dependencyRoot "x64/bin"
$colladaInclude = Join-Path $vcpkgInstalled "include/collada-dom2.5"
$colladaLibrary = Join-Path $vcpkgInstalled "lib/collada-dom2.5-dp-vc140-mt.lib"
$colladaDebugLibrary = Join-Path $vcpkgInstalled "debug/lib/collada-dom2.5-dp-vc140-mt.lib"
$uriparserLibrary = Join-Path $vcpkgInstalled "lib/uriparser.lib"
$luajitInclude = Join-Path $dependencyRoot "x64/include/LuaJIT"
$luajitLibrary = Join-Path $dependencyRoot "x64/lib/lua51.lib"

if (-not (Test-Path $toolchain)) {
    throw "The vcpkg toolchain was not found."
}

if (-not (Test-Path (Join-Path $sdlInclude "SDL_opengl.h"))) {
    throw "The bundled SDL2 OpenGL headers were not found."
}

if (-not (Test-Path $sdlLibrary)) {
    throw "The bundled SDL2 UWP library was not found."
}

if (-not (Test-Path $colladaLibrary)) {
    throw "The UWP COLLADA DOM library was not found."
}

if (-not (Test-Path (Join-Path $luajitInclude "lua.h")) -or
    -not (Test-Path (Join-Path $luajitInclude "luajit.h")) -or
    -not (Test-Path $luajitLibrary) -or
    -not (Test-Path (Join-Path $runtimeDir "lua51.dll"))) {
    throw "The UWP LuaJIT headers and library were not found."
}

$cmakeArgs = @(
    "-S", $sourceRoot,
    "-B", $buildRoot,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_SYSTEM_NAME=WindowsStore",
    "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=x64-uwp-static-md",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    "-DOPENMW_USE_SYSTEM_OSG=OFF",
    "-DOSG_STATIC=ON",
    "-DOSG_WINDOWING_SYSTEM=None",
    "-DOSG_USE_UTF8_FILENAME=ON",
    "-DOPENGL_PROFILE=GL2",
    "-DOPENGL_INCLUDE_DIR=$sdlInclude",
    "-DOPENGL_gl_LIBRARY=opengl32",
    "-DOPENGL_HEADER1=#include <SDL_opengl.h>",
    "-DSDL2_INCLUDE_DIR=$sdlInclude",
    "-DSDL2_LIBRARY=$sdlLibrary",
    "-DSDL2_DIR=$cmakeRoot",
    "-D_OPENTHREADS_ATOMIC_USE_WIN32_INTERLOCKED=1",
    "-DCOLLADA_INCLUDE_DIR=$colladaInclude",
    "-DCOLLADA_STATIC_LIBRARY=$colladaLibrary",
    "-DCOLLADA_STATIC_LIBRARY_DEBUG=$colladaDebugLibrary",
    "-DCOLLADA_URIPARSER_LIBRARY=$uriparserLibrary",
    "-DCOLLADA_BOOST_FILESYSTEM_LIBRARY=",
    "-DCOLLADA_BOOST_SYSTEM_LIBRARY=",
    "-DCOLLADA_PCRECPP_LIBRARY=",
    "-DCOLLADA_PCRE_LIBRARY=",
    "-DLuaJit_INCLUDE_DIR=$luajitInclude",
    "-DLuaJit_LIBRARY=$luajitLibrary",
    "-DOPENMW_UWP_RUNTIME_DIR=$runtimeDir",
    "-DBUILD_LAUNCHER=OFF",
    "-DBUILD_WIZARD=OFF",
    "-DBUILD_MWINIIMPORTER=OFF",
    "-DBUILD_OPENCS=OFF",
    "-DBUILD_ESSIMPORTER=OFF",
    "-DBUILD_BSATOOL=OFF",
    "-DBUILD_ESMTOOL=OFF",
    "-DBUILD_NIFTEST=OFF",
    "-DBUILD_NAVMESHTOOL=OFF",
    "-DBUILD_BULLETOBJECTTOOL=OFF",
    "-DBUILD_BENCHMARKS=OFF",
    "-DOPENMW_USE_SYSTEM_BULLET=ON",
    "-DOPENMW_USE_SYSTEM_MYGUI=OFF",
    "-DMYGUI_STATIC=ON",
    "-DOPENMW_USE_SYSTEM_RECASTNAVIGATION=OFF",
    "-DRECASTNAVIGATION_STATIC=ON",
    "-DOPENMW_USE_SYSTEM_SQLITE3=OFF",
    "-DOPENMW_USE_SYSTEM_YAML_CPP=OFF",
    "-DYAML_CPP_STATIC=ON"
)

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& cmake --build $buildRoot --config $Configuration --target openmw --parallel
exit $LASTEXITCODE
