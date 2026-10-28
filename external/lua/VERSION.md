Lua 5.5.1 (official stable release)

Source: https://www.lua.org/ftp/lua-5.5.1.tar.gz
SHA256: 1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce
License: MIT, full notice in lua-5.5.1/src/lua.h and lua-5.5.1/doc/readme.html.
Official compatibility/build reference: https://www.lua.org/download.html and https://www.lua.org/manual/5.5/manual.html

Unmodified upstream sources. MSVC v145, C++ compilation, static library, /MDd (Debug) or /MD (Release). Lua supports C++ compilation and then uses C++ exceptions internally, preserving RAII during protected errors. Core API headers are included directly (not lua.hpp); no sol2, Lua DLL, interpreter installation or native module loading is needed. lua.c/luac.c executables are excluded.
