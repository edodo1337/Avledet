# Avledet server image. Build and run with docker compose, see README.md.

# Toolchain + vcpkg deps, the same steps as port/build/avledet-build.Dockerfile
FROM ubuntu:24.04 AS toolchain

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

FROM toolchain AS build

# Steamworks SDK headers and libsteam_api.so from the steamworks-sys crate (same interfaces as the game)
ARG STEAMWORKS_SYS=0.13.0
RUN mkdir -p /steamsdk \
    && curl -fsSL https://crates.io/api/v1/crates/steamworks-sys/${STEAMWORKS_SYS}/download | tar -xz -C /opt \
    && ln -s /opt/steamworks-sys-${STEAMWORKS_SYS}/lib/steam /steamsdk/sdk

COPY CMakeLists.txt vcpkg.json steam_appid.txt /src/
COPY cmake /src/cmake
COPY core /src/core
COPY library /src/library
COPY data /src/data
RUN cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_MANIFEST_MODE=OFF \
        -DLUA_INCLUDE_DIR=/opt/vcpkg/installed/x64-linux-dynamic/include \
        -DLUA_LIBRARY=/opt/vcpkg/installed/x64-linux-dynamic/lib/liblua.so \
        -DSTEAMWORKS_SDK=/steamsdk \
    && cmake --build /build --target avledet_server

# steamclient.so is not in the SDK, Steam game servers load it from ~/.steam/sdk64
FROM steamcmd/steamcmd:ubuntu-24 AS steamcmd

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libstdc++6 libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

LABEL org.opencontainers.image.source=https://github.com/edodo1337/Avledet

# binary and libs in /srv, data template in /opt/avledet/data (copied to the volume on start).
# No game data in the image, it is mounted from game-data/out (docker compose run --rm dump).
COPY --from=build /build/bin/avledet_server /build/bin/liblua.so /build/bin/libsteam_api.so /build/bin/steam_appid.txt /srv/
COPY --from=build /build/bin/data /opt/avledet/data
COPY --from=steamcmd /root/.local/share/Steam/steamcmd/linux64/steamclient.so /opt/avledet/
COPY port/docker/entrypoint.sh /opt/avledet/
RUN rm -f /opt/avledet/data/server.yml && mkdir -p /srv/data && chmod 777 /srv/data

ENV HOME=/tmp/avledet LD_LIBRARY_PATH=/srv TRACY_NO_INVARIANT_CHECK=1
WORKDIR /srv
EXPOSE 2456/udp 2457/udp
ENTRYPOINT ["/opt/avledet/entrypoint.sh"]
