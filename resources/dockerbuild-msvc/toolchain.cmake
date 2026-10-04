# Windows CMake runs under Wine with the compiler/SDK environment established
# by entrypoint.sh; there is no installed Visual Studio instance or vcvarsall.
include("Z:/build/tools/vcpkg/scripts/toolchains/windows.cmake")
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)
set(CMAKE_C_COMPILER "$ENV{MSVC_WINE_CL}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "$ENV{MSVC_WINE_CL}" CACHE FILEPATH "")
set(CMAKE_LINKER "$ENV{MSVC_WINE_LINK}" CACHE FILEPATH "")
set(CMAKE_RC_COMPILER "$ENV{MSVC_WINE_RC}" CACHE FILEPATH "")
set(CMAKE_RC_FLAGS_INIT "$ENV{MSVC_WINE_RC_FLAGS}")
set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "")
