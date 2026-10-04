# Retain upstream's static-library/dynamic-CRT and networking policies.
include("${CMAKE_CURRENT_LIST_DIR}/x86-windows-static-md.cmake")
set(VCPKG_BUILD_TYPE release)
set(VCPKG_LOAD_VCVARS_ENV OFF)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../resources/dockerbuild-msvc/toolchain.cmake")
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED
    PATH INCLUDE LIB LIBPATH MSVC_WINE_CL MSVC_WINE_LINK MSVC_WINE_RC MSVC_WINE_RC_FLAGS)
