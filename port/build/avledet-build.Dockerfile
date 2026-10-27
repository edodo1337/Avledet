# Toolchain + vcpkg deps for building Avledet. Source and Steamworks SDK are mounted at build time.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++-14 gcc-14 cmake ninja-build git curl zip unzip tar pkg-config \
        autoconf automake autoconf-archive libtool python3 ca-certificates linux-libc-dev \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && rm -rf /var/lib/apt/lists/*

# pinned to match dependency versions listed in Avledet's CMakeLists (mid 2025)
ARG VCPKG_TAG=2025.07.25
RUN git clone --depth 1 --branch ${VCPKG_TAG} https://github.com/microsoft/vcpkg /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/opt/vcpkg
RUN /opt/vcpkg/vcpkg install \
        abseil asio gtest gtl intrusive-shared-ptr magic-enum nlohmann-json opus quill \
        range-v3 sol2 tracy unordered-dense yaml-cpp zlib zstd \
    && /opt/vcpkg/vcpkg install lua:x64-linux-dynamic \
    && rm -rf /opt/vcpkg/buildtrees /opt/vcpkg/downloads /opt/vcpkg/packages

# PersistentEventSystem stores its events Brotli compressed in the .db2
RUN /opt/vcpkg/vcpkg install brotli \
    && rm -rf /opt/vcpkg/buildtrees /opt/vcpkg/downloads /opt/vcpkg/packages

WORKDIR /src
