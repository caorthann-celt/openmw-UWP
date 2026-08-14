# UWP dependencies

This folder contains the matched x64 UWP SDL2, Mesa and LuaJIT files used by the Xbox build. The layout follows the original [uwp-dep](https://github.com/worleydl/uwp-dep) depot so the headers, import libraries and runtime DLLs stay together.

SDL2 2.28.5 is based on the [SDL UWP OpenGL fork](https://github.com/aerisarn/SDL-uwp-gl) at commit `b8454648ff1ca435baeb25b497814cfddd854321`. The bundled build includes Xbox window sizing and Game Bar changes.

The Mesa WGL runtime comes from [SternXD's UWP 26.2.0 release](https://github.com/SternXD/mesa-uwp/releases/tag/26.2.0). Its supporting UWP DLLs come from [uwp-dep](https://github.com/worleydl/uwp-dep) at commit `0ec87d2406b47743772e2787654275a6f6c0e284`.

LuaJIT comes from the [official repository](https://github.com/LuaJIT/LuaJIT) at commit `4886b676a698acc4bbdf54adfabb3e33a8c020e8`.

Licences for every bundled component are in `lic`.
