# Build the patched vcpkg executable's own CURL dependency with native CMake.
# Schannel avoids OpenSSL's Windows-host-only nmake build during bootstrapping.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/toolchains/windows.cmake")
set(ENV{CC} /build/tools/msvc/bin/x64/cl)
set(ENV{CXX} /build/tools/msvc/bin/x64/cl)
set(ENV{PATH} "/build/tools/msvc/bin/x64:$ENV{PATH}")
