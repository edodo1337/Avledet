# Game data dump: SteamCMD, .NET SDK for the dumper plugin and the libs of the vanilla dedicated server.
# Run with: docker compose run --rm dump (see README.md)
FROM steamcmd/steamcmd:ubuntu-24

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl unzip python3 dotnet-sdk-8.0 \
        libatomic1 libpulse0 libpulse-mainloop-glib0 libsdl2-2.0-0 libdbus-1-3 libsndfile1 libsystemd0 libx11-6 libxcb1 \
    && rm -rf /var/lib/apt/lists/*

COPY port/tools/avldump-server /dumper
COPY port/tools/make_location_meta.py port/docker/dump.sh /opt/

ENV DOTNET_NOLOGO=1 DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_GENERATE_ASPNET_CERTIFICATE=false
ENTRYPOINT ["/opt/dump.sh"]
