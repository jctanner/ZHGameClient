FROM debian:12-slim

# Build arguments
ARG CMAKE_VERSION="3.31.6"
ARG GIT_VERSION="2.49.0"
ARG UID=1000
ARG GID=1000

# Install system dependencies (Wine, Python for msvc-wine, and build tools)
RUN apt-get update \
    && dpkg --add-architecture i386 \
    && apt-get install -y --no-install-recommends \
        wget gpg unzip git ca-certificates \
        python3 msitools \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install Wine from WineHQ
RUN mkdir -pm755 /etc/apt/keyrings \
    && wget -O - https://dl.winehq.org/wine-builds/winehq.key | gpg --dearmor -o /etc/apt/keyrings/winehq-archive.key - \
    && wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/debian/dists/bookworm/winehq-bookworm.sources \
    && apt-get update \
    && apt-get install -y --no-install-recommends winehq-stable \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Setup build folder
RUN mkdir -p /build/tools /build/tmp /build/tmp/home \
    && chown -R ${UID}:${GID} /build

USER ${UID}:${GID}
WORKDIR /build/tools

# Install Windows CMake
RUN wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-windows-x86_64.zip \
    && unzip -q cmake-${CMAKE_VERSION}-windows-x86_64.zip -d /build/tools/ \
    && mv /build/tools/cmake-${CMAKE_VERSION}-windows-x86_64 /build/tools/cmake \
    && rm -f *.zip

# Install Windows Git
RUN wget -q https://github.com/git-for-windows/git/releases/download/v${GIT_VERSION}.windows.1/MinGit-${GIT_VERSION}-64-bit.zip \
    && unzip -q MinGit-${GIT_VERSION}-64-bit.zip -d /build/tools/ \
    && mv /build/tools/cmd /build/tools/git \
    && rm -f *.zip

# Install Windows Ninja
RUN wget -q https://github.com/ninja-build/ninja/releases/download/v1.13.1/ninja-win.zip \
    && unzip -q ninja-win.zip -d /build/tools/ \
    && rm -f *.zip

# Install MSVC Build Tools via msvc-wine
RUN git clone --depth 1 https://github.com/mstorsjo/msvc-wine.git /build/tools/msvc-wine

# Download MSVC 2022 and Windows SDK (x86 target for 32-bit game)
# --msvc-version 17 = VS 2022, host x64, target x86
ENV WINEDEBUG=-all
ENV WINEARCH=win64
ENV WINEPREFIX=/build/prefix64
ENV HOME=/build/tmp/home

RUN /build/tools/msvc-wine/vsdownload.py \
        --dest /build/tools/msvc \
        --accept-license \
    && /build/tools/msvc-wine/install.sh /build/tools/msvc

# Setup environment for MSVC 2022
# The install.sh creates wrapper scripts in msvc/bin/x86/ and msvc/bin/x64/
# We use x86 for 32-bit compilation
ENV CC="Z:\\build\\tools\\msvc\\bin\\x86\\cl.exe"
ENV CXX="Z:\\build\\tools\\msvc\\bin\\x86\\cl.exe"
ENV PRESET=msvc-wine

# Create empty TEMP folder for linking
ENV TMP="Z:\\build\\tmp"
ENV TEMP="Z:\\build\\tmp"
ENV TEMPDIR="Z:\\build\\tmp"

WORKDIR /build/cnc

USER root
# Keep these layers after the large MSVC download. The manifest supplies the
# baseline so the container and CMake use the same vcpkg revision.
ARG VCPKG_BASELINE
RUN test -n "$VCPKG_BASELINE" \
    && git clone https://github.com/microsoft/vcpkg.git /build/tools/vcpkg \
    && git -C /build/tools/vcpkg checkout "$VCPKG_BASELINE"
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        winbind zip curl build-essential cmake ninja-build pkg-config \
        autoconf automake libtool bison nasm perl \
    && rm -rf /var/lib/apt/lists/* \
    && /build/tools/vcpkg/bootstrap-vcpkg.sh -disableMetrics
# This is the source revision reported by the manifest baseline's vcpkg tool.
# Its long-path removal code needs one separator normalization for Wine.
ARG VCPKG_TOOL_COMMIT=98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8
RUN git init /build/tools/vcpkg-tool \
    && git -C /build/tools/vcpkg-tool remote add origin https://github.com/microsoft/vcpkg-tool.git \
    && git -C /build/tools/vcpkg-tool fetch --depth 1 origin "$VCPKG_TOOL_COMMIT" \
    && git -C /build/tools/vcpkg-tool checkout FETCH_HEAD
COPY vcpkg-wine-paths.patch /build/tools/vcpkg-wine-paths.patch
COPY bootstrap-triplets /build/tools/bootstrap-triplets
RUN export WINEPREFIX=/tmp/vcpkg-wineprefix TMP=/build/tmp TEMP=/build/tmp TMPDIR=/build/tmp \
    && wineboot --init 2>/dev/null \
    && CC=gcc CXX=g++ /build/tools/vcpkg/vcpkg install \
        'curl[core,ssl]:x64-windows-msvc-wine-bootstrap' --classic \
        --overlay-triplets=/build/tools/bootstrap-triplets \
        --x-install-root=/build/tools/vcpkg-bootstrap-installed
RUN git -C /build/tools/vcpkg-tool apply /build/tools/vcpkg-wine-paths.patch \
    && export WINEPREFIX=/tmp/vcpkg-wineprefix TMP=/build/tmp TEMP=/build/tmp TMPDIR=/build/tmp \
    && wineboot --init 2>/dev/null \
    && PATH="/build/tools/msvc/bin/x64:$PATH" cmake \
        -S /build/tools/vcpkg-tool -B /build/tools/vcpkg-tool/build -G Ninja \
        -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=/build/tools/msvc/bin/x64/cl \
        -DCMAKE_CXX_COMPILER=/build/tools/msvc/bin/x64/cl \
        -DCMAKE_RC_COMPILER=/build/tools/msvc/bin/x64/rc \
        -DCMAKE_TOOLCHAIN_FILE=/build/tools/vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_INSTALLED_DIR=/build/tools/vcpkg-bootstrap-installed \
        -DVCPKG_TARGET_TRIPLET=x64-windows-msvc-wine-bootstrap \
        -DVCPKG_OVERLAY_TRIPLETS=/build/tools/bootstrap-triplets -DVCPKG_MANIFEST_INSTALL=OFF \
        -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded \
        -DVCPKG_DEVELOPMENT_WARNINGS=OFF -DVCPKG_MSVC_ANALYZE=OFF -DBUILD_TESTING=OFF \
        -DVCPKG_BASE_VERSION=2026-07-27 -DVCPKG_VERSION="$VCPKG_TOOL_COMMIT-wine" \
    && PATH="/build/tools/msvc/bin/x64:$PATH" cmake --build /build/tools/vcpkg-tool/build --target vcpkg --parallel 4 \
    && cp /build/tools/vcpkg-tool/build/vcpkg.exe /build/tools/vcpkg/vcpkg.exe
# A baseline update must also review the pinned tool revision and Wine patch.
RUN . /build/tools/vcpkg/scripts/vcpkg-tool-metadata.txt \
    && test "$VCPKG_TOOL_RELEASE_TAG" = 2026-07-27
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
CMD ["/entrypoint.sh"]
