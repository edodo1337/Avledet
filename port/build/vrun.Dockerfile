# Runtime for Avledet and the vanilla dedicated server (tools/bot, tools/run-*.sh): docker build -t vrun:1 -f vrun.Dockerfile .
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libatomic1 libpulse0 \
        libpulse-mainloop-glib0 libsdl2-2.0-0 libdbus-1-3 libsndfile1 libsystemd0 libstdc++6 libgcc-s1 libx11-6 libxcb1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /srv
